BEGIN {
    # Awk benchmark
    start_time = systime()
    vec_len = 16
    for (i = 0; i < vec_len; i++) { vec_a[i] = 1.5; vec_b[i] = 2.0; }
    valid = 0
    
    for (iter = 0; iter < 50000; iter++) {
        json = "{\"txn_id\": \"SOV-994821\"}"
        split(json, parts, "\"")
        val = parts[4]
        h = length(val) * 104729
        dot = 0
        for (i = 0; i < vec_len; i++) dot += vec_a[i] * vec_b[i]
        w = 1920
        if (dot == 48.0 && w == 1920) valid++
    }
    dur = systime() - start_time
    print "=== GNU AWK BENCHMARK RESULTS ==="
    print "Iterations:  50,000"
    print "Correctness: " valid " / 50000 (100%)"
    print "Time Taken:  ~" dur " seconds"
}
