from gradelib import *

r = Runner(save("log"))

@test(4, "Public: DBP Test 1")
def public_test1_dbp():
    r.run_qemu(
        shell_script(["test1 DBP"]),
        make_args=["CPUS=1", "INJECT=trap"],
        timeout=30
    )
    r.match(
        r"dispatch process#4 at 0: allocated_time=2",
        r"process#4 finish one cycle at 2: 5 cycles left",
        r"dispatch process#5 at 2: allocated_time=3",
        r"process#5 finish one cycle at 5: 4 cycles left",
        r"dispatch process#4 at 5: allocated_time=2",
        r"process#4 finish one cycle at 7: 4 cycles left",
        r"dispatch process#5 at 7: allocated_time=3",
        r"process#5 finish one cycle at 10: 3 cycles left",
        r"dispatch process#4 at 10: allocated_time=2",
        r"process#4 finish one cycle at 12: 3 cycles left",
        r"dispatch process#5 at 13: allocated_time=3",
        r"process#5 finish one cycle at 16: 2 cycles left",
        r"dispatch process#4 at 16: allocated_time=2",
        r"process#4 finish one cycle at 18: 2 cycles left",
        r"dispatch process#5 at 19: allocated_time=1",
        r"dispatch process#4 at 20: allocated_time=2",
        r"process#4 finish one cycle at 22: 1 cycles left",
        r"dispatch process#5 at 22: allocated_time=2",
        r"process#5 finish one cycle at 24: 1 cycles left",
        r"dispatch process#4 at 25: allocated_time=2",
        r"process#4 finish one cycle at 27: 0 cycles left",
        r"dispatch process#5 at 27: allocated_time=3",
        r"process#5 finish one cycle at 30: 0 cycles left",
    )

@test(4, "Public: DBP Test 2")
def public_test2_dbp():
    r.run_qemu(
        shell_script(["test2 DBP"]),
        make_args=["CPUS=1", "INJECT=trap"],
        timeout=30
    )
    r.match(
        r"dispatch process#5 at 0: allocated_time=2",
        r"process#5 finish one cycle at 2: 5 cycles left",
        r"dispatch process#4 at 2: allocated_time=1",
        r"process#4 misses a deadline at 3 in scheduler",
        r"dispatch process#4 at 3: allocated_time=2",
        r"process#4 finish one cycle at 5: 4 cycles left",
        r"dispatch process#5 at 5: allocated_time=1",
        r"process#5 misses a deadline at 6 in scheduler",
        r"dispatch process#5 at 6: allocated_time=2",
        r"process#5 finish one cycle at 8: 3 cycles left",
        r"dispatch process#4 at 8: allocated_time=1",
        r"process#4 misses a deadline at 9 in scheduler",
        r"dispatch process#5 at 9: allocated_time=2",
        r"process#5 finish one cycle at 11: 2 cycles left",
        r"dispatch process#4 at 11: allocated_time=1",
        r"process#4 misses a deadline at 12 in scheduler",
        r"dispatch process#4 at 12: allocated_time=2",
        r"process#4 finish one cycle at 14: 1 cycles left",
        r"dispatch process#5 at 14: allocated_time=1",
        r"process#5 misses a deadline at 15 in scheduler",
        r"dispatch process#5 at 15: allocated_time=2",
        r"process#5 finish one cycle at 17: 0 cycles left",
        r"dispatch process#4 at 17: allocated_time=1",
        r"process#4 misses a deadline at 18 in scheduler",
    )

@test(4, "Public: DBP Test 3")
def public_test3_dbp():
    r.run_qemu(
        shell_script(["test3 DBP"]),
        make_args=["CPUS=1", "INJECT=trap"],
        timeout=30
    )
    r.match(
        r"dispatch process#6 at 0: allocated_time=1",
        r"process#6 finish one cycle at 1: 2 cycles left",
        r"dispatch process#4 at 1: allocated_time=2",
        r"process#4 finish one cycle at 3: 1 cycles left",
        r"dispatch process#5 at 3: allocated_time=2",
        r"dispatch process#6 at 5: allocated_time=1",
        r"process#6 finish one cycle at 6: 1 cycles left",
        r"dispatch process#5 at 6: allocated_time=1",
        r"process#5 finish one cycle at 7: 1 cycles left",
        r"dispatch process#6 at 10: allocated_time=1",
        r"process#6 finish one cycle at 11: 0 cycles left",
        r"dispatch process#4 at 11: allocated_time=2",
        r"process#4 finish one cycle at 13: 0 cycles left",
        r"dispatch process#5 at 15: allocated_time=3",
        r"process#5 finish one cycle at 18: 0 cycles left",
    )

@test(4, "Public: DBP Test 4")
def public_test4_dbp():
    r.run_qemu(
        shell_script(["test4 DBP"]),
        make_args=["CPUS=1", "INJECT=trap"],
        timeout=30
    )
    r.match(
        r"dispatch process#4 at 0: allocated_time=2",
        r"process#4 finish one cycle at 2: 4 cycles left",
        r"dispatch process#6 at 2: allocated_time=1",
        r"process#6 finish one cycle at 3: 3 cycles left",
        r"dispatch process#5 at 3: allocated_time=1",
        r"dispatch process#4 at 4: allocated_time=2",
        r"process#4 finish one cycle at 6: 3 cycles left",
        r"process#5 misses a deadline at 6 in scheduler",
        r"dispatch process#6 at 6: allocated_time=1",
        r"process#6 finish one cycle at 7: 2 cycles left",
        r"dispatch process#5 at 7: allocated_time=1",
        r"dispatch process#4 at 8: allocated_time=2",
        r"process#4 finish one cycle at 10: 2 cycles left",
        r"dispatch process#5 at 10: allocated_time=1",
        r"process#5 finish one cycle at 11: 1 cycles left",
        r"dispatch process#6 at 11: allocated_time=1",
        r"process#6 finish one cycle at 12: 1 cycles left",
        r"dispatch process#4 at 12: allocated_time=2",
        r"process#4 finish one cycle at 14: 1 cycles left",
        r"dispatch process#5 at 14: allocated_time=1",
        r"dispatch process#6 at 15: allocated_time=1",
        r"process#6 finish one cycle at 16: 0 cycles left",
        r"dispatch process#4 at 16: allocated_time=2",
        r"process#4 finish one cycle at 18: 0 cycles left",
        r"process#5 misses a deadline at 18 in scheduler",
    )

@test(4, "Public: DBP Test 5")
def public_test5_dbp():
    r.run_qemu(
        shell_script(["test5 DBP"]),
        make_args=["CPUS=1", "INJECT=trap"],
        timeout=30
    )
    r.match(
        r"dispatch process#5 at 0: allocated_time=1",
        r"process#5 finish one cycle at 1: 5 cycles left",
        r"dispatch process#4 at 1: allocated_time=2",
        r"process#4 finish one cycle at 3: 2 cycles left",
        r"dispatch process#5 at 3: allocated_time=1",
        r"process#5 finish one cycle at 4: 4 cycles left",
        r"process#6 misses a deadline at 4 in scheduler",
        r"dispatch process#6 at 4: allocated_time=1",
        r"process#6 finish one cycle at 5: 1 cycles left",
        r"dispatch process#5 at 5: allocated_time=1",
        r"process#5 finish one cycle at 6: 3 cycles left",
        r"dispatch process#4 at 6: allocated_time=2",
        r"process#4 finish one cycle at 8: 1 cycles left",
        r"process#5 misses a deadline at 8 in scheduler",
        r"dispatch process#5 at 8: allocated_time=1",
        r"process#5 finish one cycle at 9: 1 cycles left",
        r"dispatch process#6 at 9: allocated_time=1",
        r"process#6 finish one cycle at 10: 0 cycles left",
        r"dispatch process#5 at 10: allocated_time=1",
        r"process#5 finish one cycle at 11: 0 cycles left",
        r"dispatch process#4 at 11: allocated_time=1",
        r"process#4 misses a deadline at 12 in scheduler",
    )
