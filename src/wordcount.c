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
#include <limits.h>  
#include <sys/param.h>  

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

// structure to hold word frequency data for top 50 words
typedef struct {
    char word[256];
    int frequency;
} word_freq_type;

// hold data needed for each thread
typedef struct {
    char *filename;
    word_freq_type *word_freqs;
    int *word_count;
    pthread_mutex_t *mutex;
} freq_thread_data_type;

int compare_freq(const void *a, const void *b) {
    word_freq_type *w1 = (word_freq_type *)a;
    word_freq_type *w2 = (word_freq_type *)b;
    return w2->frequency - w1->frequency;  // sort in descending order
}

int find_word(word_freq_type *freqs, int count, const char *word) {
    if (!freqs || !word) return -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(freqs[i].word, word) == 0) {
            return i;
        }
    }
    return -1;
}

void process_word(char *word, word_freq_type *freqs, int *count, pthread_mutex_t *mutex) {
    if (!word || !freqs || !count || !mutex) return;
    
    // ignore single character words and very long words
    if (strlen(word) <= 1 || strlen(word) >= 255) return;
    
    // convert word to lowercase
    for (int i = 0; word[i]; i++) {
        word[i] = tolower(word[i]);
    }
    
    pthread_mutex_lock(mutex);
    int idx = find_word(freqs, *count, word);
    if (idx >= 0) {
        freqs[idx].frequency++;
    } else if (*count < 1000) {
        strncpy(freqs[*count].word, word, 255);
        freqs[*count].word[255] = '\0';
        freqs[*count].frequency = 1;
        (*count)++;
    }
    pthread_mutex_unlock(mutex);
}

void *count_frequencies(void *arg) {
    freq_thread_data_type *data = (freq_thread_data_type *)arg;
    if (!data || !data->filename) {
        pthread_exit(NULL);
    }

    FILE *file = fopen(data->filename, "r");
    if (!file) {
        pthread_exit(NULL);
    }

    char word[256];
    int c;
    int pos = 0;

    while ((c = fgetc(file)) != EOF) {
        if (isalpha(c) || isdigit(c) || c == '_' || c == '-') {
            if (pos < 255) {
                word[pos++] = c;
            }
        } else if (pos > 0) {
            word[pos] = '\0';
            process_word(word, data->word_freqs, data->word_count, data->mutex);
            pos = 0;
        }
    }

    if (pos > 0) {
        word[pos] = '\0';
        process_word(word, data->word_freqs, data->word_count, data->mutex);
    }

    fclose(file);
    pthread_exit(NULL);
}

void analyze_word_frequencies(char *directory, char *files[], int num_files) {
    if (!directory || !files || num_files <= 0) {
        printf("Invalid arguments to analyze_word_frequencies\n");
        return;
    }

    // allocate and initialize word frequency array
    word_freq_type *word_freqs = calloc(1000, sizeof(word_freq_type));
    if (!word_freqs) {
        printf("Memory allocation failed\n");
        return;
    }

    int word_count = 0;
    pthread_mutex_t mutex;
    if (pthread_mutex_init(&mutex, NULL) != 0) {
        printf("Mutex initialization failed\n");
        free(word_freqs);
        return;
    }

    // calculate number of threads needed
    int num_threads = (num_files < MAX_THREADS) ? num_files : MAX_THREADS;
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    freq_thread_data_type *thread_data = malloc(num_threads * sizeof(freq_thread_data_type));
    
    if (!threads || !thread_data) {
        printf("Thread memory allocation failed\n");
        free(word_freqs);
        pthread_mutex_destroy(&mutex);
        free(threads);
        free(thread_data);
        return;
    }

    // process files in batches if num_files > MAX_THREADS
    for (int i = 0; i < num_files; i += MAX_THREADS) {
        int batch_size = (num_files - i < MAX_THREADS) ? (num_files - i) : MAX_THREADS;
        
        // create threads for current batch
        for (int j = 0; j < batch_size; j++) {
            char filepath[PATH_MAX];
            snprintf(filepath, sizeof(filepath), "%s/%s", directory, files[i + j]);
            
            thread_data[j].filename = strdup(filepath);
            thread_data[j].word_freqs = word_freqs;
            thread_data[j].word_count = &word_count;
            thread_data[j].mutex = &mutex;
            
            if (pthread_create(&threads[j], NULL, count_frequencies, &thread_data[j]) != 0) {
                printf("Thread creation failed\n");
                for (int k = 0; k < j; k++) {
                    pthread_join(threads[k], NULL);
                    free(thread_data[k].filename);
                }
                continue;
            }
        }
        
        // wait for current batch to complete
        for (int j = 0; j < batch_size; j++) {
            pthread_join(threads[j], NULL);
            free(thread_data[j].filename);
        }
    }

    // sort words by frequency
    if (word_count > 0) {
        qsort(word_freqs, word_count, sizeof(word_freq_type), compare_freq);
        
        // print top 50 words
        printf("\nTop 50 most frequent words:\n");
        printf("%-20s %s\n", "Word", "Frequency");
        printf("----------------------------------------\n");
        int print_count = (word_count < 50) ? word_count : 50;
        for (int i = 0; i < print_count; i++) {
            printf("%-20s %d\n", word_freqs[i].word, word_freqs[i].frequency);
        }
        
        // save to file for histograms in Python
        FILE *output = fopen("word_frequencies.txt", "w");
        if (output) {
            fprintf(output, "word,frequency\n"); 
            for (int i = 0; i < word_count; i++) {
                fprintf(output, "%s,%d\n", word_freqs[i].word, word_freqs[i].frequency);
            }
            fclose(output);
            printf("\nWord frequencies saved to 'word_frequencies.txt'\n");
        }
    } else {
        printf("No words found in the processed files\n");
    }

    pthread_mutex_destroy(&mutex);
    free(thread_data);
    free(threads);
    free(word_freqs);
}

int main(int argc, char *argv[]) {
    // print usage and handle top50 arg
    if (argc < 3 || (strcmp(argv[argc - 1], "--top50") != 0 && argc < 4)) {
        printf("Usage: %s <program> <directory> <word> <mode>\n", argv[0]);
        return 1;
    }

    char *directory = argv[1];
    char *word = NULL;
    char *files[] = {"bib", "paper1", "paper2", "progc", "progl", "progp", "trans"};
    int num_files = sizeof(files) / sizeof(files[0]);
    int total_count = 0;
    char *mode = argv[argc - 1];

    if (strcmp(mode, "--top50") != 0) {
        word = argv[2];  
    } 
    
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

    } else if (strcmp(mode, "--top50") == 0) {
        analyze_word_frequencies(directory, files, num_files);
    
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