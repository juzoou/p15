from check_solve import check_solved

if __name__ == "__main__":
    import sys
    ok, res = check_solved(sys.argv[1])
    ok_line = "OK" if ok else "FAIL"
    print(ok_line, res, sep="\n")
    if not ok: 
        exit(1)
    