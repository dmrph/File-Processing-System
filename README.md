# File Processing System (Outline from Canvas)

## Project Overview

This project aims to develop a system that processes multiple large files in parallel using **multiprocessing** and **multithreading**, while employing **inter-process communication (IPC)** mechanisms for message passing between processes. The project allows students to compare the advantages and disadvantages of multiprocessing versus multithreading in terms of performance, resource consumption, and complexity.

## Problem Description

The system should accept a directory containing multiple large text files. The goal is to count the frequency of a specific word (or set of words) in each file. Each file is processed in a separate process using `fork()`. Inside each process, multiple threads should be created to read and process portions of the file in parallel.

## Key Components

1. **Multiprocessing:** 
    - The system will create a separate process for each file using the `fork()` method. Each process runs independently, ensuring that different files are processed in parallel without interfering with each other.
    
2. **Multithreading:** 
    - Within each process, multiple threads are spawned to handle different parts of the file concurrently. This improves the processing speed of each file by dividing the workload into smaller chunks processed in parallel.
    
3. **Inter-Process Communication (IPC):** 
    - The processes need to communicate the results (e.g., word counts) back to a central controller. IPC mechanisms such as pipes or message queues will be used to ensure efficient communication between processes.
    
4. **Word Frequency Counting:** 
    - The core task is to count the occurrences of a specific word or a set of words in each file. Each thread will process a portion of the file, and the results will be aggregated across threads and processes.

## Objectives

- Compare **multiprocessing** and **multithreading** in terms of:
    - Performance (execution time)
    - Resource consumption (memory, CPU usage)
    - Complexity (code complexity, debugging difficulty)

- Demonstrate effective use of **IPC mechanisms** for message passing between processes.

## Challenges

- Managing resource contention between threads.
- Ensuring that communication between processes is efficient and does not become a bottleneck.
- Handling large files efficiently to avoid memory overuse.

## Expected Outcomes

- A system that efficiently processes large files in parallel.
- Insights into the trade-offs between multiprocessing and multithreading.
- A demonstration of effective IPC techniques.

## Files from instructions

Some files from The Calgary Corpus: [Calgary Corpus](https://corpus.canterbury.ac.nz/descriptions/#calgary)

There are 14 files in this corpus, but you will process only the following 7 files:

| File   | Abbrev | Category                       | Size   |
|--------|--------|---------------------------------|--------|
| bib    | bib    | Bibliography (refer format)     | 111261 |
| paper1 | paper1 | Technical paper                 | 53161  |
| paper2 | paper2 | Technical paper                 | 82199  |
| progc  | progc  | Source code in "C"              | 39611  |
| progl  | progl  | Source code in LISP             | 71646  |
| progp  | progp  | Source code in PASCAL           | 49379  |
| trans  | trans  | Transcript of terminal session  | 93695  |
