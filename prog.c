#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

struct args
{
    char *filename;   // -f
    int dimension;    // -d
    int threads;      // -t
    double precision; // -p
} args;

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

/*
 * Relaxes array 'a' and stores the values in array 'b'.
 */
void relax_array(double **a, double **b)
{

    for (int i = 1; i < args.dimension - 1; i++)
    {
        for (int j = 1; j < args.dimension - 1; j++)
        {
            b[i][j] = (a[i - 1][j] + a[i + 1][j] + a[i][j - 1] + a[i][j + 1]) / 4;
        }
    }
}

/*
 * Calculates the precision of the array, where 'a' is the array with new values, and 'b' is the array with old values.
 */
double calculate_precision(double **a, double **b)
{
    // max precision is the biggest difference between old values and new values
    double maxPrecision = 0;

    for (int i = 1; i < args.dimension - 1; i++)
    {
        for (int j = 1; j < args.dimension - 1; j++)
        {
            double precision = fabs(b[i][j] - a[i][j]);
            if (precision > maxPrecision)
            {
                maxPrecision = precision;
            }
        }
    }

    return maxPrecision;
}

void alloc_memory(double ***a_out, double ***b_out, double **a_buf_out, double **b_buf_out)
{
    // allocate memory for pointers which will point to the "nested arrays" inside the "main 2d arrays"
    // one to read from, and one to write to, so that neighbors arent affected
    double **a = malloc(args.dimension * sizeof(double *));
    double **b = malloc(args.dimension * sizeof(double *));

    // allocate memory for pointer to the whole "2d array"
    double *a_buf = malloc(args.dimension * args.dimension * sizeof(double));
    double *b_buf = malloc(args.dimension * args.dimension * sizeof(double));

    if (a == NULL || b == NULL || a_buf == NULL || b_buf == NULL)
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

    alloc_memory(&a, &b, &a_buf, &b_buf);

    read_array(a, b);

    printf("Original array:\n");
    print_array(a);

    double precision;
    do
    {
        relax_array(a, b);
        precision = calculate_precision(a, b);
        // swapping the arrays, ensures results are always stored in 'a'
        swap_array(&a, &b);
    } while (precision > args.precision);

    printf("Final array:\n");
    print_array(a);

    // deallocate memory
    free(a);
    free(b);
    free(a_buf);
    free(b_buf);

    return 0;
}
