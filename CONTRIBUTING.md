# DevGovernor — Team Contribution & Git Workflow Guide

Welcome to the team! This repository is organized so that all three teammates can work simultaneously without stepping on each other's toes or creating git merge conflicts.

---

## 1. Team Allocation & File Boundaries

To prevent merge conflicts, keep all your code modifications inside your designated module directory:

| Student | Assigned Role | Workspace Directory | Dedicated Branch |
| :--- | :--- | :--- | :--- |
| **Student 1** | **Process Monitoring & /proc Intelligence** | `module1_monitor/` | `main` |
| **Student 2** | **Resource Management & Scheduling Governor** | `module2_governor/` | `student2-governor-rules` |
| **Student 3** | **Analytics & Terminal Dashboard** | `module3_analytics/` | `student3-dashboard-analytics` |

---

## 2. Quick Setup Instructions for Teammates

### Step 1: Clone the Repository
```bash
git clone https://github.com/Harshads-git/devgovernor.git
cd devgovernor
```

### Step 2: Switch to Your Feature Branch
- **Student 2:**
  ```bash
  git checkout student2-governor-rules
  ```
- **Student 3:**
  ```bash
  git checkout student3-dashboard-analytics
  ```

### Step 3: Verify the Build & Run Tests
Make sure everything compiles and passes before you make changes:
```bash
# In Linux or WSL2:
make clean && make test
```

---

## 3. Fast-Track for Student 3 (Offline / Windows Friendly)

If Student 3 wants to work directly on Windows (or doesn't have WSL running), sample data is provided so you can run the dashboard immediately with standard Python 3:

```bash
python module3_analytics/dashboard.py --input data/processes.sample.json --history data/actions.sample.jsonl
```
No C compiler or Linux `/proc` needed to develop and test dashboard enhancements!

---

## 4. How to Submit Your Work

1. **Commit your changes locally:**
   ```bash
   git add moduleX_yourmodule/ tests/
   git commit -m "feat(moduleX): description of your improvement"
   ```
2. **Push to your branch:**
   ```bash
   git push origin <your-branch-name>
   ```
3. **Open a Pull Request (PR):**
   - Go to https://github.com/Harshads-git/devgovernor
   - Click **Compare & pull request**
   - Our automated **GitHub Actions CI** will automatically run `make test` on Ubuntu to verify your code!
   - Student 1 will review and merge into `main`.

---

## 5. Lab Viva Checklist for the Team

- [ ] Does your code follow the data contract defined in `shared/schema.json`?
- [ ] Does `make test` pass with 0 errors and 0 warnings?
- [ ] Are all functions commented so you can explain them to the external examiner?
- [ ] Do you know your module's OS concepts from `docs/architecture.md`?
