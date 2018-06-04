# Shared memory
The aim of this project is to use low-level primitives for parallelism on a 
shared memory architecture to implement the relaxation technique for solving 
differential equations. This is achieved by having an array of values and 
repeatedly replacing values with the average of its four neighbours, except 
boundary values which remain fixed, until all values settle down to a given 
precision. The solution is written in C using p\_threads.

## Compile
Compile the program using:  
`gcc -pthread -Wall -Wextra -Wconversion prog.c -std=gnu99 -o prog`  

## Run
Run the program using:  
`./prog -d1000 -p1 -t16`  

Use flags  
	`-d` to specify the dimensions of the array  
	`-p` to specify the required precision  
	`-t` to speciy the number of threads  

## Debug

To print array values after each iteration,  
Compile the program in debug mode using:  
`gcc -pthread -Wall -Wextra -Wconversion prog.c -std=gnu99 -o prog -DDEBUG`

Then run the program normally.
