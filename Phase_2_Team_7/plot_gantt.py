import matplotlib.pyplot as plt

import re

LOGFILE = "scheduler.log"

# Regex to parse lines
pattern = re.compile(
    r"At time (\d+)\s+process (\d+)\s+(\w+)"
)

events = []
processes = set()

with open(LOGFILE) as f:
    for line in f:
        m = pattern.search(line)
        if m:
            time = int(m.group(1))
            pid = int(m.group(2))
            state = m.group(3)
            events.append((time, pid, state))
            processes.add(pid)

processes = sorted(list(processes))

# Build execution segments per process
timeline = {pid: [] for pid in processes}

# For each process, detect intervals
last_time = {pid: None for pid in processes}
running = {pid: False for pid in processes}

for time, pid, state in events:
    if state == "Started" or state == "Resumed":
        running[pid] = True
        last_time[pid] = time

    elif state == "Stopped" or state == "Finished":
        if running[pid] and last_time[pid] is not None:
            timeline[pid].append((last_time[pid], time - last_time[pid]))
        running[pid] = False
        last_time[pid] = None

# Plot
fig, ax = plt.subplots(figsize=(12, 6))

y_labels = []
y_positions = []

for i, pid in enumerate(processes):
    y = i
    y_labels.append(f"Process {pid}")
    y_positions.append(y)
    
    for start, duration in timeline[pid]:
        ax.broken_barh([(start, duration)], (y - 0.4, 0.8), facecolors='tab:blue')

ax.set_yticks(y_positions)
ax.set_yticklabels(y_labels)
ax.set_xlabel("Time")
ax.set_title("scheduler graph – Gantt Chart")


plt.grid(axis='x')
plt.tight_layout()
plt.savefig("gantt.png", dpi=300)
print("Generated gantt.png")
