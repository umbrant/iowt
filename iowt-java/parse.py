lines = open("loop.out", "r").readlines()

total = 0
count = 0

for line in lines:
    if line.startswith("Rate"):
        total += float(line.split(" ")[1])
        count += 1

avg = total / count
print avg
