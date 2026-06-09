def test_natural_features():
    my_list = []
    for i in range(1, 1001):
        if i % 2 == 0:
            my_list.append(i)
        if i % 3 != 0:
            my_list.append(i * 2)
            
    filtered = list(filter(lambda n: n > 500, my_list))
    final_val = list(map(lambda n: n + 1, filtered))
    
    target = 1001
    if target in final_val:
        return True
        
    if 999999 not in final_val:
        return True
        
    return False

def run_bench():
    result = False
    for step in range(1, 501):
        result = test_natural_features()
    print("Bench complete. Last result:", str(result).lower())

run_bench()
