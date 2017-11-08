#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define DEBUG

struct program_args
{
    char *filename;   // -f
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
 * Reads the values of the initial array from file into 'a' and 'b'.
 * Initially both 'a' and 'b' will be the same.
 */
void read_array()
{
    FILE *file = fopen(args.filename, "r");
    for (int i = 0; i < args.dimension; i++)
    {
        for (int j = 0; j < args.dimension; j++)
        {
            fscanf(file, "%lf", &a[i][j]);
            b[i][j] = a[i][j];
        }
    }
}

void relax_section(struct thread_args *thread_args)
{
    char is_start = 1;
    int cells_relaxed = 0;

    for (int i = 1; i < args.dimension - 1; i++)
    {
        for (int j = 1; j < args.dimension - 1; j++)
        {
            // jump to the starting position of this thread
            if (is_start)
            {
                i = thread_args->start_row;
                j = thread_args->start_col;
                is_start = 0;
            }

            if (cells_relaxed == thread_args->cells_to_relax)
                return;

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

            cells_relaxed++;
        }
    }
}

void relax_section_thread(struct thread_args *thread_args)
{
    while (should_continue)
    {
        relax_section(thread_args);

        // wait for other threads to complete their part
        pthread_barrier_wait(&barrier);

        // wait until main thread prepares data for next iteration
        // and checks whether we need to continue
        pthread_barrier_wait(&barrier);
    }
}

void relax_section_main(struct thread_args *thread_args)
{
    relax_section(thread_args);

    if (args.threads > 1)
    {
        // wait for other threads to complete their part
        pthread_barrier_wait(&barrier);
    }

    // swap so that results are in 'a' for next iteration
    // other threads will be waiting at second barrier
    swap_array(&a, &b);

#ifdef DEBUG
    print_array(a);
#endif
}

void relax_array(struct thread_args *all_threads_args)
{
    // since main thread will do first section
    // we only need to create "args.threads - 1" threads
    pthread_t threads[args.threads - 1];

    // no need for barrier if only one thread
    if (args.threads > 1)
    {
        pthread_barrier_init(&barrier, NULL, (unsigned int)args.threads);

        for (int i = 1; i < args.threads; i++)
        {
            pthread_create(&threads[i], NULL,
                           (void *(*)(void *))relax_section_thread,
                           (void *)&all_threads_args[i]);
        }
    }

    while (1)
    {
        // main thread will do first section
        relax_section_main(&all_threads_args[0]);

        // is_done can be set to false (0) by any thread,
        // that hasn't reached its precision
        if (is_done)
        {
            if (args.threads > 1)
            {
                // if the threads are done, setting should_continue to 0,
                // ensures the threads will exit the loop when they carry on
                should_continue = 0;
                pthread_barrier_wait(&barrier);
            }

            break;
        }

        // if any of the threads haven't reached their precision,
        // reset is_done to true and carry on to the next iteration
        is_done = 1;

        if (args.threads > 1)
        {
            // allow other threads to go on to the next iteration
            pthread_barrier_wait(&barrier);
        }
    }

    if (args.threads > 1)
    {
        for (int j = 1; j < args.threads; j++)
        {
            pthread_join(threads[j], NULL);
        }

        pthread_barrier_destroy(&barrier);
    }
}

void alloc_memory(
    double **a_buf_out, 
    double **b_buf_out, 
    struct thread_args **all_threads_args_out)
{
    // memory for each thread arguments
    struct thread_args *all_threads_args = malloc(
        (unsigned long)args.threads * sizeof(struct thread_args));

    // memory for array
    a = malloc((unsigned long)args.dimension * sizeof(double *));
    b = malloc((unsigned long)args.dimension * sizeof(double *));

    double *a_buf = malloc(
        (unsigned long)(args.dimension * args.dimension) * sizeof(double));
    double *b_buf = malloc(
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
        a[i] = a_buf + args.dimension * i;
        b[i] = b_buf + args.dimension * i;
    }

    // TODO: fix this
    *a_buf_out = a_buf;
    *b_buf_out = b_buf;

    *all_threads_args_out = all_threads_args;
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
            all_threads_args[i].cells_to_relax += 1;
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
    if (argc != 5)
    {
        printf("unexpected number of arguments.");
        exit(EXIT_FAILURE);
    }

    int opt;
    const char *optstring = "f:d:t:p:";
    while ((opt = getopt(argc, argv, optstring)) != -1)
    {
        switch (opt)
        {
        case 'f':
            args.filename = optarg;
            printf("using input file: %s\n", args.filename);
            break;
        case 'd':
            args.dimension = atoi(optarg);
            printf("using dimension: %d\n", args.dimension);
            break;
        case 't':
            args.threads = atoi(optarg);
            printf("using threads: %d\n", args.threads);
            break;
        case 'p':
            args.precision = atof(optarg);
            printf("using precision: %lf\n", args.precision);
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

    double *a_buf = NULL;
    double *b_buf = NULL;
    struct thread_args *all_threads_args = NULL;

    alloc_memory(&a_buf, &b_buf, &all_threads_args);
    alloc_work(all_threads_args);

    read_array();

    printf("Input array:\n");
    print_array(a);
    relax_array(all_threads_args);
    printf("Output array:\n");
    print_array(a);

    // deallocate memory
    free(a);
    free(b);
    free(a_buf);
    free(b_buf);
    free(all_threads_args);

    exit(EXIT_SUCCESS);
}
