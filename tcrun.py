import os
import subprocess
import re
import time
import blessed

INPUTS_DIR = "inputs"
OUTPUTS_DIR = "outputs"
RESULTS_FILE = "results.log"

if os.name == 'nt':
    MAIN_EXECUTABLE = "main.exe"
    CHECKER_EXECUTABLE = "format_checker.exe"
else:
    MAIN_EXECUTABLE = "./main"
    CHECKER_EXECUTABLE = "./format_checker"

def compile_project(term):
    print(term.bold("--- Step 1: Compiling Project ---"))
    commands = {
        "Cleaning": ["make", "clean"],
        "Compiling Main": ["make"],
        "Compiling Checker": ["make", "checker"]
    }

    for description, command in commands.items():
        print(f"Running '{' '.join(command)}'...", end='', flush=True)
        try:
            subprocess.run(
                command,
                check=True,
                capture_output=True,
                text=True
            )
            print(term.green(" OK"))
        except FileNotFoundError:
            print(term.red(" FAILED"))
            print(term.red(f"\nError: '{command[0]}' command not found. Is make installed and in your PATH?"))
            return False
        except subprocess.CalledProcessError as e:
            print(term.red(" FAILED"))
            print(term.red("\n--- Compiler Error ---"))
            print(e.stderr)
            print(term.red("----------------------"))
            return False

    print(term.green("Compilation successful.\n"))
    return True

def run_testcase(tc_name, term):
    input_path = os.path.join(INPUTS_DIR, tc_name)
    output_path = os.path.join(OUTPUTS_DIR, tc_name)

    try:
        with open(input_path, 'r') as f:
            time_limit_minutes = int(f.readline().strip())
            time_limit_seconds = time_limit_minutes * 60
    except (IOError, ValueError):
        print(term.red(" FAILED (Could not read time limit)"))
        return -1, "Read Error"

    status_line_start = f"[{tc_name:<20}] Running ({time_limit_minutes} min limit)..."
    print(status_line_start, end='', flush=True)

    start_time = time.time()
    try:
        main_process = subprocess.Popen(
            [MAIN_EXECUTABLE, input_path, output_path],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        main_process.wait(timeout=time_limit_seconds)
    except FileNotFoundError:
        print(term.move_x(len(status_line_start)) + term.red(f"FAILED ({MAIN_EXECUTABLE} not found)"))
        return -1, "Exec Not Found"
    except subprocess.TimeoutExpired:
        main_process.kill()
        duration = time.time() - start_time
        print(term.move_x(len(status_line_start)) + term.yellow(f"Timeout ({duration:.2f}s)"))
        return 0, "Timeout"
    except Exception:
        print(term.move_x(len(status_line_start)) + term.red("FAILED (Runtime Error)"))
        return -1, "Runtime Error"

    run_duration = time.time() - start_time
    check_status = f"Done ({run_duration:.2f}s) | Checking..."
    print(term.move_x(len(status_line_start)) + check_status, end='', flush=True)

    score, status = check_existing_output(tc_name, term, len(status_line_start) + len(check_status))
    return score, status

def draw_selection_screen(term, testcases, selected, key_map_inv):
    print(term.clear + term.move_y(0))
    print(term.bold("Test Case Runner"))
    print("Toggle: " + term.yellow("[0-9,a-z]") + " individual, " + term.yellow("[S/M/L]") + " by size, " + term.yellow("[A]") + " all, " + term.yellow("[H]") + " Meet TCs, " + term.yellow("[N]") + " Nishchay TCs")
    print("Actions: " + term.yellow("[R]") + " Run, " + term.yellow("[U]") + " Re-check Outputs, " + term.yellow("[q]") + " Quit")
    print("-" * 60)

    for i, tc_name in enumerate(testcases):
        key = key_map_inv[tc_name]
        selector = f"[{key}]"

        if tc_name in selected:
            print(term.green(f" {selector} {tc_name}"))
        else:
            print(f" {selector} {tc_name}")

def check_existing_output(tc_name, term, offset=0):
    input_path = os.path.join(INPUTS_DIR, tc_name)
    output_path = os.path.join(OUTPUTS_DIR, tc_name)

    if offset == 0:
        status_line_start = f"[{tc_name:<20}] Checking output..."
        print(status_line_start, end='', flush=True)
        offset = len(status_line_start)

    if not os.path.exists(output_path):
        print(term.move_x(offset) + term.yellow(" SKIPPED (No output file)"))
        return -1, "Output Missing"

    try:
        checker_result = subprocess.run(
            [CHECKER_EXECUTABLE, input_path, output_path],
            capture_output=True, text=True, timeout=30
        )
    except FileNotFoundError:
        print(term.move_x(offset) + term.red(f"FAILED ({CHECKER_EXECUTABLE} not found)"))
        return -1, "Checker Not Found"
    except subprocess.TimeoutExpired:
        print(term.move_x(offset) + term.red("FAILED (checker timed out)"))
        return -1, "Checker Timeout"

    checker_output = checker_result.stdout
    if "All constraints satisfied" in checker_output:
        score_match = re.search(r"FINAL SCORE:\s*(-?[\d.eE+-]+)", checker_output)
        if score_match:
            score = float(score_match.group(1))
            status = "Scored"
            final_status_msg = f" OK | Score: {score:,.2f}"
            print(term.move_x(offset) + term.green(final_status_msg))
        else:
            score = -1
            status = "Score Not Found"
            print(term.move_x(offset) + term.red("FAILED (Score missing)"))
    else:
        score = -1
        status = "Constraints Failed"
        print(term.move_x(offset) + term.red("FAILED (Constraints)"))

    return score, status

def regenerate_results(term, testcases):
    print(term.clear)
    print(term.bold(f"--- Re-generating Results from Existing Outputs ---"))

    results = []
    with open(RESULTS_FILE, 'w') as log_file:
        log_file.write("Test Case, Score, Status\n")

    for tc_name in sorted(testcases):
        score, status = check_existing_output(tc_name, term)
        results.append({"tc": tc_name, "score": score, "status": status})
        with open(RESULTS_FILE, 'a') as log_file:
            log_file.write(f"{tc_name}, {score}, {status}\n")

    print_final_table(term, results)

def print_final_table(term, results):
    print(term.bold(f"\n--- Final Results --- (Full log in {RESULTS_FILE})"))
    print("-" * 60)
    print(f"{'Test Case':<25} | {'Score':>15} | {'Status'}")
    print("-" * 60)

    total_score = 0.0
    original_36 = 0.0
    total_meet = 0.0
    total_nishchay = 0.0

    # Fixed list members for the "ORIGINAL 36" bucket (besides gm_* which are 30 files)
    original_36_extras = {"n1.txt", "s1.txt", "s2.txt", "s3.txt", "rk1.txt", "rk2.txt"}

    for res in results:
        name = res['tc']
        score = res['score']
        status = res['status']

        # Running total (positive scores only)
        if score > 0:
            total_score += score
            if name.startswith("gm_") or name in original_36_extras:
                original_36 += score
            if name.startswith("m"):
                total_meet += score
            if name.startswith("n"):
                total_nishchay += score

        # Row formatting
        status_color = term.green
        if status != 'Scored':
            status_color = term.yellow if status in ['Timeout', 'Output Missing'] else term.red

        if score == -1:
            score_str = term.red(f"{score:15.2f}")
        elif score == 0:
            score_str = term.yellow(f"{score:15.2f}")
        else:
            score_str = f"{score:15.2f}"

        print(f"{name:<25} | {score_str} | " + status_color(status))

    print("-" * 60)
    print(f"{term.bold('TOTAL SCORE (ORIGINAL 36)'):<35} | {term.bold_green(f'{original_36:15,.2f}')} |")
    print(f"{term.bold('TOTAL SCORE (NISHCHAY)'):<35} | {term.bold_green(f'{total_nishchay:15,.2f}')} |")
    print(f"{term.bold('TOTAL SCORE (MEET)'):<35} | {term.bold_green(f'{total_meet:15,.2f}')} |")
    print(f"{term.bold('TOTAL SCORE'):<35} | {term.bold_green(f'{total_score:15,.2f}')} |")
    print("-" * 60)

def main():
    term = blessed.Terminal()
    if not os.path.isdir(INPUTS_DIR):
        print(term.red(f"Error: Input directory '{INPUTS_DIR}' not found."))
        return

    testcases = sorted([f for f in os.listdir(INPUTS_DIR) if f.endswith(".txt")])
    if not testcases:
        print(term.yellow(f"No testcases found in '{INPUTS_DIR}'."))
        return

    keys = "0123456789abcdefghijklmnopqrstuvwxyz!@#$%^&*()_+-=[]{};:'\",.<>/?|`~\\"
    key_map = {keys[i]: tc for i, tc in enumerate(testcases)}
    key_map_inv = {tc: keys[i] for i, tc in enumerate(testcases)}
    selected = set(testcases)

    with term.cbreak(), term.hidden_cursor():
        to_run = False
        recheck = False
        while True:
            draw_selection_screen(term, testcases, selected, key_map_inv)
            key = term.inkey()
            if key == 'A':
                if len(selected) == len(testcases):
                    selected.clear()
                else:
                    selected = set(testcases)
            elif key == 'S':
                small_tcs = {tc for tc in testcases if tc.endswith("_s.txt")}
                if small_tcs.issubset(selected):
                    selected -= small_tcs
                else:
                    selected |= small_tcs
            elif key == 'M':
                medium_tcs = {tc for tc in testcases if tc.endswith("_m.txt")}
                if medium_tcs.issubset(selected):
                    selected -= medium_tcs
                else:
                    selected |= medium_tcs
            elif key == 'L':
                large_tcs = {tc for tc in testcases if tc.endswith("_l.txt")}
                if large_tcs.issubset(selected):
                    selected -= large_tcs
                else:
                    selected |= large_tcs
            elif key == 'H':
                m_tcs = {tc for tc in testcases if tc.startswith("m")}
                if m_tcs.issubset(selected):
                    selected -= m_tcs
                else:
                    selected |= m_tcs
            elif key == 'N':
                n_tcs = {tc for tc in testcases if tc.startswith("n")}
                if n_tcs.issubset(selected):
                    selected -= n_tcs
                else:
                    selected |= n_tcs
            elif key == 'U':
                recheck = True
                break
            elif key == 'R':
                to_run = True
                break
            elif key in key_map:
                tc = key_map[key]
                if tc in selected:
                    selected.remove(tc)
                else:
                    selected.add(tc)

    print(term.clear)

    if recheck:
        regenerate_results(term, testcases)
        return

    if not to_run or not selected:
        print("No testcases selected. Exiting.")
        return

    if not compile_project(term):
        return

    os.makedirs(OUTPUTS_DIR, exist_ok=True)

    print(term.bold(f"--- Step 2: Running {len(selected)} Selected Test Cases ---"))

    results = []
    with open(RESULTS_FILE, 'w') as log_file:
        log_file.write("Test Case, Score, Status\n")

    for tc_name in sorted(list(selected)):
        score, status = run_testcase(tc_name, term)
        results.append({"tc": tc_name, "score": score, "status": status})
        with open(RESULTS_FILE, 'a') as log_file:
            log_file.write(f"{tc_name}, {score}, {status}\n")

    print_final_table(term, results)


if __name__ == "__main__":
    main()

