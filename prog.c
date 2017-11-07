#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

struct program_args
{
    char *filename;   // -f
    int dimension;    // -d
    int threads;      // -t
    double precision; // -p
} args;

struct thread_args
{
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

void print_array(double **a)
{
    for (int i = 0; i < args.dimension; i++)
    {
        for (int j = 0; j < args.dimension; j++)
        {
            printf("%lf\t", a[i][j]);
        }
        printf("\n");
    }
}

/*
 * Reads the values of the initial array from file into 'a' and 'b'.
 * Initially both 'a' and 'b' will be the same.
 */
void read_array(double **a, double **b)
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

            double north = thread_args->a[i - 1][j];
            double south = thread_args->a[i + 1][j];
            double west = thread_args->a[i][j - 1];
            double east = thread_args->a[i][j + 1];

            thread_args->b[i][j] = (north + east + south + west) / 4;

            double precision = fabs(thread_args->b[i][j] - thread_args->a[i][j]);
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

        // ensures results are always stored in 'a',
        // at the start of next iteration
        swap_array(&thread_args->a, &thread_args->b);

        // wait until main thread prints the array
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

    // ensures results are always stored in 'a',
    // at the start of next iteration
    swap_array(&thread_args->a, &thread_args->b);

    // print while other threads are waiting at second barrier
    printf("\n");
    print_array(thread_args->a);
}

void relax_array(struct thread_args *all_threads_args)
{
    // -1 because main thread will do first section
    pthread_t threads[args.threads - 1];

    // no need for barrier if only one thread
    if (args.threads > 1)
    {
        pthread_barrier_init(&barrier, NULL, (unsigned int)args.threads);

        for (int i = 1; i < args.threads; i++)
        {
            pthread_create(&threads[i], NULL, (void *(*)(void *))relax_section_thread, (void *)&all_threads_args[i]);
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
                // if the threads are done,
                // setting should_continue to 0,
                // ensures the threads will exit the loop when they carry on
                should_continue = 0;
                pthread_barrier_wait(&barrier);
            }

            break;
        }

        // if any of the threads haven't reached their precision
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

void alloc_memory(double ***a_out, double ***b_out, double **a_buf_out, double **b_buf_out, struct thread_args **all_threads_args_out)
{
    // allocate memory for each thread's arguments
    struct thread_args *all_threads_args = malloc((unsigned long)args.threads * sizeof(struct thread_args));

    // allocate memory for pointers which will point to the "nested arrays" inside the "main 2d arrays"
    // one to read from, and one to write to, so that neighbors arent affected
    double **a = malloc((unsigned long)args.dimension * sizeof(double *));
    double **b = malloc((unsigned long)args.dimension * sizeof(double *));

    // allocate memory for pointer to the whole "2d array"
    double *a_buf = malloc((unsigned long)(args.dimension * args.dimension) * sizeof(double));
    double *b_buf = malloc((unsigned long)(args.dimension * args.dimension) * sizeof(double));

    if (a == NULL || b == NULL || a_buf == NULL || b_buf == NULL || all_threads_args == NULL)
    {
        printf("malloc failed.");
        exit(EXIT_FAILURE);
    }

    // each a[i] points to start of a "nested array"
    for (int i = 0; i < args.dimension; i++)
    {
        a[i] = a_buf + args.dimension * i;
        b[i] = b_buf + args.dimension * i;
    }

    *a_out = a;
    *b_out = b;
    *a_buf_out = a_buf;
    *b_buf_out = b_buf;

    *all_threads_args_out = all_threads_args;
}

void alloc_work(double **a, double **b, struct thread_args *all_threads_args)
{
    // each thread will relax n cells, where n is cells_to_relax
    int cells_to_relax = (args.dimension - 2) * (args.dimension - 2) / args.threads;

    // first m cells will relax n + 1, where m is extra_cells and n is cells_to_relax
    int extra_cells = (args.dimension - 2) * (args.dimension - 2) % args.threads;

    // although first thread should start at 1,1, starting at 0,0,
    // makes calculating the starting point of the next thread easier
    // to get the actual starting point we later add 1,1
    int row = 0, col = 0;

    for (int i = 0; i < args.threads; i++)
    {
        all_threads_args[i].a = a;
        all_threads_args[i].b = b;

        all_threads_args[i].start_row = row + 1;
        all_threads_args[i].start_col = col + 1;

        all_threads_args[i].cells_to_relax = cells_to_relax;
        if (i < extra_cells)
        {
            all_threads_args[i].cells_to_relax += 1;
        }

        // calculate the row,col of where the next thread should start
        // args.dimension - 2, ensures we are ignoring the boundary
        // integer division gives us the number of rows the thread will relax
        // modulus will give us the column number
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

    double **a = NULL;
    double **b = NULL;
    double *a_buf = NULL;
    double *b_buf = NULL;
    struct thread_args *all_threads_args = NULL;

    alloc_memory(&a, &b, &a_buf, &b_buf, &all_threads_args);
    alloc_work(a, b, all_threads_args);

    read_array(a, b);
    relax_array(all_threads_args);

    // deallocate memory
    free(a);
    free(b);
    free(a_buf);
    free(b_buf);
    free(all_threads_args);

    exit(EXIT_SUCCESS);
}
