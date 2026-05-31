#!/bin/bash
echo "=== Running all 20 DSA Complex Problem Tests ==="
total_time=0

for file in dsa_*.glow; do
    echo "Running $file..."
    
    # Run the file, capture time output
    # Since each file might not have an explicit `os.time()` wrapper around the logic,
    # we can use the `time` command to measure it.
    
    start_time=$(date +%s.%N)
    ../bin/glowlang "$file" > /dev/null 2>&1
    end_time=$(date +%s.%N)
    
    elapsed=$(echo "$end_time - $start_time" | bc)
    total_time=$(echo "$total_time + $elapsed" | bc)
    
    printf "%-30s : %0.3f seconds\n" "$(basename $file)" $elapsed
done

echo "================================================="
printf "Total Execution Time: %0.3f seconds\n" $total_time
