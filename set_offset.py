import csv

if __name__=='__main__':
    csv_path = "jlcpcb/production_files/CPL-MB-RL7021-11-M5StickC.csv"
    f = open(csv_path, "r")
    d = csv.reader(f)
    data = [e for e in d]
    f.close()

    print(data)

    for i in range(len(data)):
        if data[i][0] == "J1":
            data[i][4] = str(float(data[i][4]) + (2.54 * 3.5)) 

    with open(csv_path, 'w', newline="") as f:
        write = csv.writer(f)
        write.writerows(data)
