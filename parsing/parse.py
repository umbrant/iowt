#!/usr/bin/python

import os
import sys

def main():
    # Init big data structure used for holding all data
    data = {}
    for k1 in ("none", "gzip"):
        data[k1] = {}
        for k2 in ("disk", "mem"):
            data[k1][k2] = {}
            for k3 in ("local", "remote"):
                data[k1][k2][k3] = []

    # Init data structure used for tail trimming
    # Figure out the min completion time to do tail trimming
    trim = {}
    for k1 in ("none", "gzip"):
        trim[k1] = {}
        for k2 in ("disk", "mem"):
            trim[k1][k2] = "9999999999999999"

    if len(sys.argv) != 2:
        print "Must specify path to data folder"
        sys.exit(-1)

    datapath = sys.argv[1]
    listing = os.listdir(datapath)

    for filename in listing:
        # Only parse benchmark files
        if not filename.startswith("benchmark"):
            continue
        print "Parsing", filename
        lines = open(datapath + "/" + filename, "r").readlines()
        # Trim off trailing newlines
        for x in range(len(lines)):
            lines[x] = lines[x][:-1]
        date = lines[0]
        ip = lines[1]
        request = lines[2].split(", ")
        size = request[0].split(": ")[1]
        compression = request[1].split(": ")[1]
        storage = request[2].split(": ")[1]
        bench_settings = lines[3]

        temp_min = 0
        for line in lines[5:]:
            if line == "":
                if temp_min < trim[compression][storage]:
                    trim[compression][storage] = temp_min
                break
            splitted = line.split(", ")
            time = splitted[0]
            host = splitted[1].split(" ")[1]
            rate = splitted[2].split(" ")[1]

            location = "remote"
            if host == ip:
                location = "local"
            data[compression][storage][location].append((time,rate))
            temp_min = time

    # Trim off all the tail data
    for k1 in data.keys():
        for k2 in data[k1].keys():
            total = 0
            counter = 0
            for k3 in data[k1][k2].keys():
                l = data[k1][k2][k3][:] # Make a copy
                for x in l:
                    total += 1
                    if x[0] > trim[k1][k2]:
                        counter += 1
                        data[k1][k2][k3].remove(x)
            print (k1,k2), counter, "of", total, "trimmed"

    # Permute data from (time, rate) to just rate
    for k1 in data.keys():
        for k2 in data[k1].keys():
            for k3 in data[k1][k2].keys():
                for x in range(len(data[k1][k2][k3])):
                    data[k1][k2][k3][x] = \
                            data[k1][k2][k3][x][1]

    open(datapath + "/" + "result_none_disk_local", "w").write( "\n".join(data["none"]["disk"]["local"]))
    open(datapath + "/" + "result_none_disk_remote", "w").write("\n".join(data["none"]["disk"]["remote"]))
    open(datapath + "/" + "result_none_mem_local", "w").write(  "\n".join(data["none"]["mem"]["local"]))
    open(datapath + "/" + "result_none_mem_remote", "w").write( "\n".join(data["none"]["mem"]["remote"]))
    open(datapath + "/" + "result_gzip_disk_local", "w").write( "\n".join(data["gzip"]["disk"]["local"]))
    open(datapath + "/" + "result_gzip_disk_remote", "w").write("\n".join(data["gzip"]["disk"]["remote"]))
    open(datapath + "/" + "result_gzip_mem_local", "w").write(  "\n".join(data["gzip"]["mem"]["local"]))
    open(datapath + "/" + "result_gzip_mem_remote", "w").write( "\n".join(data["gzip"]["mem"]["remote"]))

    # Do a spreadsheet friendly version
    csv = ",".join(["", "", \
                    "", "", \
                    "", "", \
                    "", ""])
    csv += "\n"
    csv += ",".join(["none disk local", "none disk remote", \
                    "none mem local", "none mem remote", \
                    "gzip disk local", "gzip disk remote", \
                    "gzip mem local", "gzip mem remote"])
    csv += "\n"
    str_maker = lambda x: (x, "")[x == None]
    tuple_maker = lambda a,b,c,d,e,f,g,h: map(str_maker,(a,b,c,d,e,f,g,h))
    csv_data = map(tuple_maker, \
                   data["none"]["disk"]["local"],\
                   data["none"]["disk"]["remote"],\
                   data["none"]["mem"]["local"],\
                   data["none"]["mem"]["remote"],\
                   data["gzip"]["disk"]["local"],\
                   data["gzip"]["disk"]["remote"],\
                   data["gzip"]["mem"]["local"],\
                   data["gzip"]["mem"]["remote"])
    csv_lines = "\n".join(",".join(x) for x in csv_data)
    csv += csv_lines

    open(datapath + "/" + "result_aggregate", "w").write(csv)

if __name__ == "__main__":
    main()
