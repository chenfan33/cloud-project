/*
 * parse.h
 *
 * File for parse parameters
 */

#ifndef PARSE_H_
#define PARSE_H_

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct Args{
	bool debug;
	bool port_set;
	int port;

	Args(bool _debug = false, bool _port_set = false, int _port = 0):
		debug(_debug), port_set(_port_set), port(_port){}
};

void parse_args(int argc, char *argv[], Args& args){
	int c;
	while ((c = getopt (argc, argv, "avp:")) != -1){
		switch (c){
			/*
			 * If the -a option is given, the server should output your full name
			 * and SEAS login to stderr, and then exit.
			 */
	    	case 'a':
	    		fprintf(stderr, "Yinda Zhang, yindaz.\n");
	    		exit(0);
	    	/*
	    	 * If the -v option is given, the server should print debug output to stderr.
	    	 */
	    	case 'v':
	    		args.debug = true;
	    		break;
	    	/*
	    	 * If the -p option is given, the server should accept connections
	    	 * on the specified port
	    	 */
	    	case 'p':
	    		args.port_set = true;
	    		args.port = atoi(optarg);
	    		if(args.port > 65535){
	    			fprintf (stderr, "Port number is out of range (0-65535).\n");
	    			abort();
	    		}
	    		break;
	    	case '?':
	    		if (optopt == 'p')
	    			fprintf (stderr, "Option -p requires an argument.\n");
	    		else if (isprint (optopt))
	    			fprintf (stderr, "Unknown option `-%c'.\n", optopt);
	    		else
	    			fprintf (stderr,
	                   "Unknown option character `\\x%x'.\n",
	                   optopt);
	    		abort();
	    	default:
	    		fprintf(stderr, "Unknown option.\n");
	    		abort();
		}
	}
}

#endif /* PARSE_H_ */
