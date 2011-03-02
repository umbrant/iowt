#!/usr/bin/python

import os
import sys

def main():
    # Init big data structure used for holding all data
    data = {}

    data["none"] = {}
    data["gzip"] = {}

    data["none"]["disk"] = {}
    data["none"]["mem"] = {}
    data["gzip"]["disk"] = {}
    data["gzip"]["mem"] = {}

    data["none"]["disk"]["local"] = []
    data["none"]["disk"]["remote"] = []
    data["none"]["mem"]["local"] = []
    data["none"]["mem"]["remote"] = []

    data["gzip"]["disk"]["local"] = []
    data["gzip"]["disk"]["remote"] = []
    data["gzip"]["mem"]["local"] = []
    data["gzip"]["mem"]["remote"] = []


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

        for line in lines[5:]:
            if line == "":
                break
            host = line.split(" ")[1][:-1]
            rate = line.split(" ")[3]
            location = "remote"
            if host == ip:
                location = "local"
            data[compression][storage][location].append(rate)

    open(datapath + "/" + "result_none_disk_local", "w").write( "\n".join(data["none"]["disk"]["local"]))
    open(datapath + "/" + "result_none_disk_remote", "w").write("\n".join(data["none"]["disk"]["remote"]))
    open(datapath + "/" + "result_none_mem_local", "w").write(  "\n".join(data["none"]["mem"]["local"]))
    open(datapath + "/" + "result_none_mem_remote", "w").write( "\n".join(data["none"]["mem"]["remote"]))
    open(datapath + "/" + "result_gzip_disk_local", "w").write( "\n".join(data["gzip"]["disk"]["local"]))
    open(datapath + "/" + "result_gzip_disk_remote", "w").write("\n".join(data["gzip"]["disk"]["remote"]))
    open(datapath + "/" + "result_gzip_mem_local", "w").write(  "\n".join(data["gzip"]["mem"]["local"]))
    open(datapath + "/" + "result_gzip_mem_remote", "w").write( "\n".join(data["gzip"]["mem"]["remote"]))


if __name__ == "__main__":
    main()
