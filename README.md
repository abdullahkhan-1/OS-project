# OS-project
# 🏥 Hospital Bed Management System

This project implements a hospital bed management system using **POSIX shared memory**, **named FIFOs**, **semaphores**, and **multithreading** in C. It supports inserting (admitting) patients, releasing (discharging) them, and searching for patients using their CNIC.

## 📁 Features

* Assigns beds to patients based on availability and critical status.
* Tracks patients in shared memory for fast concurrent access.
* Uses named FIFOs and semaphores for safe interprocess communication.
* Supports the following operations from the client:

  * `insert` – Admit a patient.
  * `release` – Discharge a patient.
  * `search` – Find a patient by CNIC.

## 🧩 Components

* `client.c`: Interacts with the user and sends requests to the server.
* `server.c`: Handles multiple clients using threads and processes requests using shared memory.
* `patient_log.txt`: Maintains logs of released patients with timestamps.

## 🛠 Build Instructions

```
gcc -o server server.c -lpthread -lrt
gcc -o client client.c -lrt
```

## 🚀 Run Instructions

**Step 1:** Start the server in one terminal:

```
./server
```

**Step 2:** Run the client in another terminal:

```
./client
```

You can run multiple clients simultaneously.

## 👨‍⚕️ Usage

When prompted in the client:

* **Insert a patient:**

  ```
  Enter command (insert/release/search/exit): insert
  Name: John Doe
  Age: 45
  CNIC: 1234512345671
  Phone: 03001234567
  Critical? (1=Yes, 0=No): 0
  ```

* **Release a patient:**

  ```
  Enter command (insert/release/search/exit): release
  Enter patient ID to release: 1
  ```

* **Search a patient:**
  ~~~
  Enter command (insert/release/search/exit): search
  Enter CNIC to search: 1234512345671
  ~~~
## 🧪 Concurrency Control

* **FIFO Semaphore (`/fifo_mutex`)** ensures that multiple clients write to the server FIFO safely.
* **Shared Memory Mutex (`/mutex`)** ensures exclusive access to the shared memory when modifying patient data.

## 📂 Shared Memory Structure

* `patients[]`: Holds patient information.
* `regular_beds[]`: Array of assigned regular beds.
* `emergency_beds[]`: Array of assigned emergency beds.
* `last_patient_id`: Used to assign unique patient IDs.

## 📝 Logs

Discharged patients are logged in `patient_log.txt` with timestamps.

## 🧹 Cleanup

If needed, you can clean up shared memory and FIFOs manually:

```bash
rm /tmp/patient_requests
rm /tmp/client_*
rm /dev/shm/patient_shm
sem_unlink("/mutex");
sem_unlink("/fifo_mutex");
```

## ⚙️ Dependencies

* Linux-based OS
* GCC
* POSIX-compliant environment
* Libraries: `pthread`, `rt`, `fcntl`, `unistd`

## 📚 Concepts Used

* Interprocess Communication (IPC)
* POSIX Shared Memory
* Named FIFOs
* Named Semaphores
* Threads and Synchronization
* File I/O for logging

## 📌 Notes

* Maximum 100 patients supported (`MAX_PATIENTS`).
* Regular beds: 10; Emergency beds: 3.
* Bed numbers are assigned uniquely and reused on release.
