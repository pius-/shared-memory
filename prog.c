#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>

struct thread_args
{
    int dimensions;
    double precision;
    double **a;
    double **b;
    int cells_to_relax;
    int start_row;
    int start_col;
};

pthread_barrier_t barrier;
char is_done = 1, should_continue = 1;

void swap_array(double ***a, double ***b)
{
    double **temp = *a;
    *a = *b;
    *b = temp;
}

void print_array(double **a, int dimensions)
{
    for (int i = 0; i < dimensions; i++)
    {
        for (int j = 0; j < dimensions; j++)
        {
            printf("%f\t", a[i][j]);
        }
        printf("\n");
    }
    printf("\n");
}

/*
 * Populates the array with values into 'a' and 'b'.
 * Initially both 'a' and 'b' will be the same.
 */
void populate_array(double ***a, double ***b, int dimensions)
{
    for (int i = 0; i < dimensions; i++)
    {
        for (int j = 0; j < dimensions; j++)
        {
            // using rand generates the same random values on multiple runs
            // as it uses the same seed
            int val = rand() % 10;
            (*a)[i][j] = val;
            (*b)[i][j] = val;
        }
    }
}

void relax_section(struct thread_args *args)
{
    char is_start = 1;
    int cells_remaining = args->cells_to_relax;

    for (int i = args->start_row; i < args->dimensions - 1; i++)
    {
        // j cant be set to start_col as it needs to be
        // reset to 1 for every new row
        for (int j = 1; j < args->dimensions - 1; j++)
        {
            if (cells_remaining == 0)
                return;

            if (is_start)
            {
                j = args->start_col;
                is_start = 0;
            }

            args->b[i][j] = (args->a[i - 1][j] 
                + args->a[i + 1][j] 
                + args->a[i][j - 1] 
                + args->a[i][j + 1]) / 4;

            if (fabs(args->b[i][j] - args->a[i][j]) > args->precision)
            {
                // this is a datarace between threads that need to continue
                // this is fine, as they are all trying to set it to 0
                // so it doesnt matter if one thread overwrites another
                is_done = 0;
            }

            cells_remaining--;
        }
    }
}

void relax_section_thread(struct thread_args *args)
{
    while (should_continue)
    {
        relax_section(args);

        // wait for other threads to complete their part
        int wait = pthread_barrier_wait(&barrier);
        if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD)
            exit(EXIT_FAILURE);

        // swap so that results are in 'a' for next iteration
        swap_array(&args->a, &args->b);

        // wait until main thread check whether we need to continue
        wait = pthread_barrier_wait(&barrier);
        if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD)
            exit(EXIT_FAILURE);
    }
}

void relax_section_main(struct thread_args *args)
{
    int iterations = 0;
    while (1)
    {
        relax_section(args);

        // wait for other threads to complete their part
        int wait = pthread_barrier_wait(&barrier);
        if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD)
            exit(EXIT_FAILURE);

        iterations++;

        // swap so that results are in 'a' for next iteration
        swap_array(&args->a, &args->b);

#ifdef DEBUG
        print_array(args->a, args->dimensions);
#endif

        // is_done can be set to false (0) by any thread,
        // that hasn't reached its precision, including this one
        if (is_done)
        {
            // if the threads are done, setting should_continue to 0,
            // ensures the threads will exit the loop when they carry on
            should_continue = 0;
            wait = pthread_barrier_wait(&barrier);
            if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD)
                exit(EXIT_FAILURE);

            break;
        }

        // if any of the threads haven't reached their precision,
        // reset is_done to true and carry on to the next iteration
        is_done = 1;

        // allow other threads to go on to the next iteration
        wait = pthread_barrier_wait(&barrier);
        if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD)
            exit(EXIT_FAILURE);
    }

    printf("iterations: %d\n", iterations);
}

void relax_array(struct thread_args *all_threads_args, int nthreads)
{
    int init = pthread_barrier_init(&barrier, NULL, (unsigned int)nthreads);
    if (init != 0)
        exit(EXIT_FAILURE);

    pthread_t threads[nthreads - 1];

    for (int i = 0; i < nthreads - 1; i++)
    {
        int create = pthread_create(&threads[i], NULL,
                                    (void *(*)(void *))relax_section_thread,
                                    (void *)&all_threads_args[i + 1]);
        if (create != 0)
            exit(EXIT_FAILURE);
    }

    // main thread will do first section
    relax_section_main(&all_threads_args[0]);

    for (int j = 0; j < nthreads - 1; j++)
    {
        int join = pthread_join(threads[j], NULL);
        if (join != 0)
            exit(EXIT_FAILURE);
    }

    int destroy = pthread_barrier_destroy(&barrier);
    if (destroy != 0)
        exit(EXIT_FAILURE);
}

void alloc_memory(
    double ***a,
    double ***b,
    double **a_buf,
    double **b_buf,
    struct thread_args **all_threads_args,
    int dimensions,
    int nthreads)
{
    // memory for each thread arguments
    *all_threads_args = malloc(
        (unsigned long)nthreads * sizeof(struct thread_args));

    // memory for array
    *a = malloc((unsigned long)dimensions * sizeof(double *));
    *b = malloc((unsigned long)dimensions * sizeof(double *));

    *a_buf = malloc(
        (unsigned long)(dimensions * dimensions) * sizeof(double));
    *b_buf = malloc(
        (unsigned long)(dimensions * dimensions) * sizeof(double));

    if (*a == NULL || *b == NULL || *a_buf == NULL || *b_buf == NULL 
        || *all_threads_args == NULL)
    {
        printf("malloc failed.");
        exit(EXIT_FAILURE);
    }

    // each a[i] points to start of a row
    for (int i = 0; i < dimensions; i++)
    {
        (*a)[i] = *a_buf + dimensions * i;
        (*b)[i] = *b_buf + dimensions * i;
    }
}

void alloc_work(double **a,
                double **b,
                struct thread_args *all_threads_args,
                int dimensions,
                int threads,
                double precision)
{
    int total_cells_to_relax = (dimensions - 2) * (dimensions - 2);

    // each thread will relax n cells, where n is cells_to_relax
    int cells_to_relax = total_cells_to_relax / threads;

    // first m cells will relax n + 1,
    // where m is extra_cells and n is cells_to_relax
    int extra_cells = total_cells_to_relax % threads;

    // to simplify starting point calculations
    // we start at 0,0 and add 1,1 afterwards
    int row = 0, col = 0;

    for (int i = 0; i < threads; i++)
    {
        all_threads_args[i].a = a;
        all_threads_args[i].b = b;
        all_threads_args[i].dimensions = dimensions;
        all_threads_args[i].precision = precision;
        all_threads_args[i].start_row = row + 1;
        all_threads_args[i].start_col = col + 1;

        all_threads_args[i].cells_to_relax = cells_to_relax;
        if (i < extra_cells)
        {
            all_threads_args[i].cells_to_relax++;
        }

        // calculate the row,col of where the next thread should start
        // * args.dimension - 2, ensures we are ignoring the boundary
        // * integer division gives us the number of rows the thread will relax
        // * modulus will give us the column number
        int offset = col + all_threads_args[i].cells_to_relax;
        row += offset / (dimensions - 2);
        col = offset % (dimensions - 2);
    }
}

void process_args(int argc, char *argv[],
                  int *dimensions, int *threads, double *precision)
{
    if (argc != 4)
    {
        printf("unexpected number of arguments.");
        exit(EXIT_FAILURE);
    }

    int opt;
    const char *optstring = "d:t:p:";
    while ((opt = getopt(argc, argv, optstring)) != -1)
    {
        switch (opt)
        {
        case 'd':
            *dimensions = atoi(optarg);
            break;
        case 't':
            *threads = atoi(optarg);
            break;
        case 'p':
            *precision = atof(optarg);
            break;
        default:
            printf("unexpected argument.");
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char *argv[])
{
    int dimensions = 0;
    int threads = 1;
    double precision = 1;

    process_args(argc, argv, &dimensions, &threads, &precision);

    printf("using dimension: %d\n", dimensions);
    printf("using threads: %d\n", threads);
    printf("using precision: %lf\n", precision);

    double **a = NULL;
    double **b = NULL;
    double *a_buf = NULL;
    double *b_buf = NULL;
    struct thread_args *all_threads_args = NULL;

    alloc_memory(&a, &b, &a_buf, &b_buf, &all_threads_args, dimensions, threads);
    alloc_work(a, b, all_threads_args, dimensions, threads, precision);

    populate_array(&a, &b, dimensions);
    relax_array(all_threads_args, threads);

    // deallocate memory
    free(a);
    free(b);
    free(a_buf);
    free(b_buf);
    free(all_threads_args);

    exit(EXIT_SUCCESS);
}
