import sys
import os
from multiprocessing import Process

def run(command):
    os.system(command)

if __name__ == "__main__":
    master = Process(target = run, args = ("./master/master", ))
    master.start()

    email = Process(target = run, args = ("./email/smtp", ))
    email.start()

    backend_ports = []
    with open("/home/cis5050/git/T01/common/backend.txt") as f:
        lines = f.readlines()
        for line in lines:
            part = line.split(',')
            if len(part) == 2:
                backend_ports.append((part[1].split(':'))[1])
    print(backend_ports)

    backends = []
    for port in backend_ports:
        backends.append(Process(target = run, 
            args = ("./backend/kvstore -p " + port, )))
        backends[-1].start()

    frontends = []
    for port in [9000, 9200]:
        frontends.append(Process(target = run, 
            args = ("./frontend/frontend -p " + str(port), )))
        frontends[-1].start()

    master.join()
    email.join()
    for backend in backends:
        backend.join()
    for frontend in frontends:
        frontend.join()