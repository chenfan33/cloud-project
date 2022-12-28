import sys
import os
import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
                    prog = 'Make',
                    description = 'Python Make')
    parser.add_argument('-v', '--verbose')
    args = parser.parse_args()
    print(int(args.verbose))

    os.system("cd backend && make CFLAGS=-DDEBUG=" + args.verbose)
    os.system("cd frontend && make CFLAGS=-DDEBUG=" + args.verbose)
    os.system("cd email && make CFLAGS=-DDEBUG=" + args.verbose)
    os.system("cd master && make CFLAGS=-DDEBUG=" + args.verbose)