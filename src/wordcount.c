#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>

#define MAX_THREADS 8
#define BUFFER_SIZE 4096

// holds shared data for each thread
typedef struct {
    pthread_mutex_t mutex;
    int word_count;
} shared_data_t;

// passing data to threads
typedef struct {
    char *filename;
    char *word;
    long start_pos;
    long end_pos;
    shared_data_t *shared_data;
    int overlap_size;  // overlap size between chunks
} thread_data_t;

// check if a character is part of a word
int is_word_char(char c) {
    return isalpha(c) || isdigit(c);
}

// count the word in a part of the file
void *count_word(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    FILE *file = fopen(data->filename, "r");
    if (!file) {
        perror("File opening failed");
        pthread_exit(NULL);
    }

    // calculate actual reading boundaries with overlap
    long actual_start = (data->start_pos == 0) ? 0 : data->start_pos - data->overlap_size;
    long actual_end = data->end_pos + data->overlap_size;
    long bytes_to_read = actual_end - actual_start;
    
    // allocate buffer
    char *buffer = malloc(bytes_to_read + 1);
    if (!buffer) {
        fclose(file);
        pthread_exit(NULL);
    }

    // read the chunk with overlap
    fseek(file, actual_start, SEEK_SET);
    size_t bytes_read = fread(buffer, 1, bytes_to_read, file);
    buffer[bytes_read] = '\0';
    
    int count = 0;
    char *ptr = buffer;
    size_t word_len = strlen(data->word);

    // process only the assigned chunk (excluding overlap) for edge chunks
    char *process_start = buffer + ((data->start_pos == 0) ? 0 : data->overlap_size);
    char *process_end = buffer + bytes_read - ((data->end_pos == actual_end) ? 0 : data->overlap_size);
    *process_end = '\0';  // null-terminate the process_end

    // count words in the assigned chunk
    while ((ptr = strstr(process_start, data->word)) != NULL && ptr < process_end) {
        // check word boundaries
        if ((ptr == process_start || !is_word_char(*(ptr - 1))) && 
            (!is_word_char(*(ptr + word_len)))) {
            count++;
        }
        process_start = ptr + 1;
    }

    // update of the shared counter
    pthread_mutex_lock(&data->shared_data->mutex);
    data->shared_data->word_count += count;
    pthread_mutex_unlock(&data->shared_data->mutex);

    free(buffer);
    fclose(file);
    pthread_exit(NULL);
}

// process a file using threads
void process_file_with_threads(char *filename, char *word, int *result) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("File open failed");
        return;
    }

    fseek(file, 0L, SEEK_END);
    long file_size = ftell(file);
    fclose(file);

    pthread_t threads[MAX_THREADS];
    thread_data_t thread_data[MAX_THREADS];
    shared_data_t shared_data = {PTHREAD_MUTEX_INITIALIZER, 0};
    
    // calculate chunk size and overlap
    long chunk_size = file_size / MAX_THREADS;
    int overlap_size = strlen(word) * 2;

    // create threads with overlapping chunks
    for (int i = 0; i < MAX_THREADS; i++) {
        thread_data[i].filename = filename;
        thread_data[i].word = word;
        thread_data[i].start_pos = i * chunk_size;
        thread_data[i].end_pos = (i == MAX_THREADS - 1) ? file_size : (i + 1) * chunk_size;
        thread_data[i].shared_data = &shared_data;
        thread_data[i].overlap_size = overlap_size;
        
        if (pthread_create(&threads[i], NULL, count_word, &thread_data[i]) != 0) {
            perror("Thread creation failed");
            return;
        }
    }

    // wait for all threads to complete
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    *result = shared_data.word_count;
    pthread_mutex_destroy(&shared_data.mutex);
}

// process a file using a single thread (for multiprocessing comparison)
void process_file_single_thread(char *filename, char *word, int *result) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("File open failed");
        return;
    }

    // get file size
    fseek(file, 0L, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    // allocate buffer for entire file
    char *buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        return;
    }

    // read entire file
    size_t bytes_read = fread(buffer, 1, file_size, file);
    buffer[bytes_read] = '\0';
    
    int count = 0;
    char *ptr = buffer;
    size_t word_len = strlen(word);

    // count words
    while ((ptr = strstr(ptr, word)) != NULL) {
        if ((ptr == buffer || !is_word_char(*(ptr - 1))) && 
            (!is_word_char(*(ptr + word_len)))) {
            count++;
        }
        ptr += 1;
    }

    *result = count;
    free(buffer);
    fclose(file);
}

int main(int argc, char *argv[]) {
    // print usage
    if (argc < 4) {
        printf("Usage: %s <program> <directory> <word> <mode>\n", argv[0]);
        return 1;
    }

    char *directory = argv[1];
    char *word = argv[2];
    char *mode = argv[3];
    char *files[] = {"bib", "paper1", "paper2", "progc", "progl", "progp", "trans"};
    int num_files = sizeof(files) / sizeof(files[0]);
    int total_count = 0;

    clock_t start = clock();

    if (strcmp(mode, "--multiprocessing") == 0) {
        int pipes[num_files][2];

        // create processes for each file
        for (int i = 0; i < num_files; i++) {
            if (pipe(pipes[i]) == -1) {
                perror("pipe failed");
                exit(EXIT_FAILURE);
            }

            pid_t pid = fork();
            if (pid == 0) {  // child process
                close(pipes[i][0]);  // close read end
                
                char filepath[256];
                snprintf(filepath, sizeof(filepath), "%s/%s", directory, files[i]);
                int file_word_count = 0;
                
                process_file_single_thread(filepath, word, &file_word_count);
                
                write(pipes[i][1], &file_word_count, sizeof(int));
                close(pipes[i][1]);
                exit(0);
            } else if (pid < 0) {
                perror("fork failed");
                exit(EXIT_FAILURE);
            }
        }

        // parent process collects results
        for (int i = 0; i < num_files; i++) {
            close(pipes[i][1]);  // close write end
            int file_word_count = 0;
            read(pipes[i][0], &file_word_count, sizeof(int));
            close(pipes[i][0]);
            total_count += file_word_count;
            printf("Count of the word '%s' in file '%s': %d\n", word, files[i], file_word_count);
        }

        // wait for all children
        for (int i = 0; i < num_files; i++) {
            wait(NULL);
        }

    } else if (strcmp(mode, "--multithreading") == 0) {
        // process all files using threads
        for (int i = 0; i < num_files; i++) {
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s/%s", directory, files[i]);
            int file_word_count = 0;
            process_file_with_threads(filepath, word, &file_word_count);
            total_count += file_word_count;
            printf("Count of the word '%s' in file '%s': %d\n", word, files[i], file_word_count);
        }
    /*
    } else if (strcmp(mode, "--top50") == 0) {
        // find top 50 word frequencies in the files
        


    }
    */
    } else {
        printf("Invalid mode: %s\n", mode);
        return 1;
    }

    clock_t end = clock();
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("Total count of the word '%s': %d\n", word, total_count);
    printf("Time taken: %f seconds\n", time_taken);

    return 0;
}