#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>

struct program_args
{
    int dimension;    // -d
    int threads;      // -t
    double precision; // -p
} args;

struct thread_args
{
    int cells_to_relax;
    int start_row;
    int start_col;
};

pthread_barrier_t barrier;
char is_done = 1, should_continue = 1;
int iterations = 0;
double **a = NULL;
double **b = NULL;

void swap_array(double ***a, double ***b)
{
    double **temp = *a;
    *a = *b;
    *b = temp;
}

void print_array(double **a)
{
    for (int i = 0; i < args.dimension; i++)
    {
        for (int j = 0; j < args.dimension; j++)
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
void populate_array()
{
    for (int i = 0; i < args.dimension; i++)
    {
        for (int j = 0; j < args.dimension; j++)
        {
            // using rand generates the same random values on multiple runs
            // as it uses the same seed
            int val = rand() % 10;
            a[i][j] = val;
            b[i][j] = val;
        }
    }
}

void relax_section(struct thread_args *thread_args)
{
    char is_start = 1;
    int cells_remaining = thread_args->cells_to_relax;

    for (int i = thread_args->start_row; i < args.dimension - 1; i++)
    {
        // j cant be set to start_col as it needs to be
        // reset to 1 for every new row
        for (int j = 1; j < args.dimension - 1; j++)
        {
            if (cells_remaining == 0)
                return;

            if (is_start)
            {
                j = thread_args->start_col;
                is_start = 0;
            }

            double north = a[i - 1][j];
            double south = a[i + 1][j];
            double west = a[i][j - 1];
            double east = a[i][j + 1];

            b[i][j] = (north + east + south + west) / 4;

            double precision = fabs(b[i][j] - a[i][j]);
            if (precision > args.precision)
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

void relax_section_thread(struct thread_args *thread_args)
{
    while (should_continue)
    {
        relax_section(thread_args);

        // wait for other threads to complete their part
        int wait = pthread_barrier_wait(&barrier);
        if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD)
            exit(EXIT_FAILURE);

        // wait until main thread prepares data for next iteration
        // and checks whether we need to continue
        wait = pthread_barrier_wait(&barrier);
        if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD)
            exit(EXIT_FAILURE);
    }
}

void relax_section_main(struct thread_args *thread_args)
{
    while (1)
    {
        relax_section(thread_args);

        if (args.threads > 1)
        {
            // wait for other threads to complete their part
            int wait = pthread_barrier_wait(&barrier);
            if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD)
                exit(EXIT_FAILURE);
        }

        // swap so that results are in 'a' for next iteration
        // other threads will be waiting at second barrier
        swap_array(&a, &b);

        iterations++;

        // is_done can be set to false (0) by any thread,
        // that hasn't reached its precision, including this one
        if (is_done)
        {
            if (args.threads > 1)
            {
                // if the threads are done, setting should_continue to 0,
                // ensures the threads will exit the loop when they carry on
                should_continue = 0;
                int wait = pthread_barrier_wait(&barrier);
                if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD)
                    exit(EXIT_FAILURE);
            }

            break;
        }

#ifdef DEBUG
        print_array(a);
#endif

        // if any of the threads haven't reached their precision,
        // reset is_done to true and carry on to the next iteration
        is_done = 1;

        if (args.threads > 1)
        {
            // allow other threads to go on to the next iteration
            int wait = pthread_barrier_wait(&barrier);
            if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD)
                exit(EXIT_FAILURE);
        }
    }
}

void relax_array(struct thread_args *all_threads_args)
{
    // since main thread will do first section
    // we only need to create "args.threads - 1" threads
    pthread_t threads[args.threads - 1];

    // no need for barrier or extra threads if only one thread
    if (args.threads > 1)
    {
        int init = pthread_barrier_init(&barrier, NULL,
                (unsigned int)args.threads);

        if (init != 0)
            exit(EXIT_FAILURE);

        for (int i = 1; i < args.threads; i++)
        {
            int create = pthread_create(&threads[i], NULL,
                    (void *(*)(void *))relax_section_thread,
                    (void *)&all_threads_args[i]);

            if (create != 0)
                exit(EXIT_FAILURE);
        }
    }

    // main thread will do first section
    relax_section_main(&all_threads_args[0]);

    if (args.threads > 1)
    {
        for (int j = 1; j < args.threads; j++)
        {
            int join = pthread_join(threads[j], NULL);
            if (join != 0)
                exit(EXIT_FAILURE);
        }

        int destroy = pthread_barrier_destroy(&barrier);
        if (destroy != 0)
            exit(EXIT_FAILURE);
    }
}

void alloc_memory(
        double **a_buf,
        double **b_buf,
        struct thread_args **all_threads_args)
{
    // memory for each thread arguments
    *all_threads_args = malloc(
        (unsigned long)args.threads * sizeof(struct thread_args));

    // memory for array
    a = malloc((unsigned long)args.dimension * sizeof(double *));
    b = malloc((unsigned long)args.dimension * sizeof(double *));

    *a_buf = malloc(
        (unsigned long)(args.dimension * args.dimension) * sizeof(double));
    *b_buf = malloc(
        (unsigned long)(args.dimension * args.dimension) * sizeof(double));

    if (a == NULL || b == NULL || a_buf == NULL || b_buf == NULL 
        || all_threads_args == NULL)
    {
        printf("malloc failed.");
        exit(EXIT_FAILURE);
    }

    // each a[i] points to start of a row
    for (int i = 0; i < args.dimension; i++)
    {
        a[i] = *a_buf + args.dimension * i;
        b[i] = *b_buf + args.dimension * i;
    }
}

void alloc_work(struct thread_args *all_threads_args)
{
    int total_cells_to_relax = (args.dimension - 2) * (args.dimension - 2);

    // each thread will relax n cells, where n is cells_to_relax
    int cells_to_relax = total_cells_to_relax / args.threads;

    // first m cells will relax n + 1,
    // where m is extra_cells and n is cells_to_relax
    int extra_cells = total_cells_to_relax % args.threads;

    // to simplify starting point calculations
    // we start at 0,0 and add 1,1 afterwards
    int row = 0, col = 0;

    for (int i = 0; i < args.threads; i++)
    {
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
        row += offset / (args.dimension - 2);
        col = offset % (args.dimension - 2);
    }
}

void process_args(int argc, char *argv[])
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
                args.dimension = atoi(optarg);
                break;
            case 't':
                args.threads = atoi(optarg);
                break;
            case 'p':
                args.precision = atof(optarg);
                break;
            default:
                printf("unexpected argument.");
                exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char *argv[])
{
    process_args(argc, argv);

    printf("using dimension: %d\n", args.dimension);
    printf("using threads: %d\n", args.threads);
    printf("using precision: %lf\n", args.precision);

    double *a_buf = NULL;
    double *b_buf = NULL;
    struct thread_args *all_threads_args = NULL;

    alloc_memory(&a_buf, &b_buf, &all_threads_args);
    alloc_work(all_threads_args);

    populate_array();

#ifdef DEBUG
    printf("\n");
    print_array(a);
#endif

    relax_array(all_threads_args);

#ifdef DEBUG
    print_array(a);
#endif

    printf("total iterations: %d\n", iterations);

    // deallocate memory
    free(a);
    free(b);
    free(a_buf);
    free(b_buf);
    free(all_threads_args);

    exit(EXIT_SUCCESS);
}
