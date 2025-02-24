#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <pthread.h>
#include <algorithm>
#include <cctype>

#define MAX_FILENAME 256

struct FileQueue {
    std::queue<std::string> files;
    pthread_mutex_t lock;
    int current_file_id;
};

struct MapperArgs {
    FileQueue* queue;
    std::unordered_map<std::string, std::unordered_set<int>>* results;
    pthread_mutex_t* result_lock;
    pthread_barrier_t* barrier;
    int thread_id;
};

struct ReducerArgs {
    std::unordered_map<std::string, std::unordered_set<int>>* results;
    pthread_mutex_t* result_lock;
    pthread_barrier_t* barrier;
    int thread_id;
    int total_mappers;
    int total_reducers;
};


void valid_word(std::string& word) {
    std::string validated_word;
    for (char c : word) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            validated_word += std::tolower(static_cast<unsigned char>(c));
        }
    }
    word = validated_word;
}


void init_queue(FileQueue& queue) {
    queue.current_file_id = 1;
    pthread_mutex_init(&queue.lock, NULL);
}

void enqueue(FileQueue& queue, const std::string& file) {
    pthread_mutex_lock(&queue.lock);
    queue.files.push(file);
    pthread_mutex_unlock(&queue.lock);
}

int dequeue(FileQueue& queue, std::string& file) {
    pthread_mutex_lock(&queue.lock);
    if (queue.files.empty()) {
        pthread_mutex_unlock(&queue.lock);
        return 0;
    }
    file = queue.files.front();
    queue.files.pop();
    int file_id = queue.current_file_id;
    queue.current_file_id++;
    pthread_mutex_unlock(&queue.lock);
    return file_id;
}

// Functie care ajuta la sortarea cuvintelor in functie de numarul fisierelor in care apar
bool compare_words(const std::pair<std::string, std::unordered_set<int>>& a,
                   const std::pair<std::string, std::unordered_set<int>>& b) {
    if (a.second.size() != b.second.size()) {
        return a.second.size() > b.second.size();
    }
    return a.first < b.first;
}

void* mapper_function(void* arg) {
    MapperArgs* args = (MapperArgs*)arg;
    FileQueue* queue = args->queue;

    std::unordered_map<std::string, std::unordered_set<int>> local_results;
    while (true) {
        std::string file;
        // Se extrage un fisier din coada
        int file_id = dequeue(*queue, file);
        if (file_id == 0) break;

        std::ifstream current_file(file);
        if (!current_file) {
            std::cerr << "Eroare la deschiderea fisierului: " << file << std::endl;
            continue;
        }

        std::string word;
        // Se citeste fisierul cuvant cu cuvant 
        while (current_file >> word) {
            // Se elimina caracterele care nu sunt alfabetice
            valid_word(word);
            if (!word.empty()) {
                // Fiecare Mapper va retine rezultatele local
                local_results[word].insert(file_id);
            }
        }
        current_file.close();
    }

    // Se adauga rezultatele locale la rezultatele finale
    pthread_mutex_lock(args->result_lock);
    for (const auto& entry : local_results) {
        (*args->results)[entry.first].insert(entry.second.begin(), entry.second.end());
    }
    pthread_mutex_unlock(args->result_lock);

    pthread_barrier_wait(args->barrier);
    return NULL;
}

void* reducer_function(void* arg) {
    ReducerArgs* args = (ReducerArgs*)arg;
    pthread_barrier_wait(args->barrier);

    int reducer_id = args->thread_id - args->total_mappers;
    int total_reducers = args->total_reducers;

    // Fiecare reducer va avea atribuit un numar de litere din alfabet
    int letters_per_reducer = 26 / total_reducers;
    // Daca impartirea nu s-a realizat uniform, literele suplimenatre vor trebui distribuite
    int extra_letters = 26 % total_reducers;
    int start_letter_index, end_letter_index;

    // Reducer daca primeste o litera suplimentara, isi va mari intervalul cu o unitate
    if (reducer_id < extra_letters) {
        start_letter_index = reducer_id * (letters_per_reducer + 1);
        end_letter_index = start_letter_index + letters_per_reducer;
    } else {
        start_letter_index = reducer_id * letters_per_reducer + extra_letters;
        end_letter_index = start_letter_index + letters_per_reducer - 1;
    }

    // Extragem cuvintele pentru literele specifice fiecarui Reducer
    std::vector<std::pair<std::string, std::unordered_set<int>>> sorted_results;
    for (auto& entry : *args->results) {
        if (!entry.first.empty()) {
            char first_letter = entry.first[0];
            if (first_letter >= 'a' + start_letter_index && first_letter <= 'a' + end_letter_index) {
                sorted_results.push_back(entry);
            }
        }
    }

    // Sortam cuvintele in functie de fisierele de intrare
    std::sort(sorted_results.begin(), sorted_results.end(), compare_words);

    for (int i = start_letter_index; i <= end_letter_index; i++) {
        char letter = 'a' + i;
        std::string filename(1, letter);
        // Pentru fiecare litera cream fisiere specifice
        filename += ".txt";
        std::ofstream output_file(filename);
        if (!output_file) {
            std::cerr << "Nu a putut fi deschis fisierul: " << filename << std::endl;
            continue;
        }

        for (auto& entry : sorted_results) {
            // Pentru fiecare litera in parte scriem cuvintele specifice impreuna cu ID-urile sortate
            if (entry.first[0] == letter) {
                output_file << entry.first << ":[";

                std::vector<int> sorted_file_ids(entry.second.begin(), entry.second.end());
                // ID-urile vor fi sortate crescator
                std::sort(sorted_file_ids.begin(), sorted_file_ids.end());
                for (size_t j = 0; j < sorted_file_ids.size(); j++) {
                    output_file << sorted_file_ids[j];
                    if (j < sorted_file_ids.size() - 1) output_file << " ";
                }
                output_file << "]" << std::endl;
            }
        }

        if (sorted_results.empty()) {
            output_file << "Nu există cuvinte care încep cu litera '" << letter << "'." << std::endl;
        }

        output_file.close();
    }
    return NULL;
}

int main(int argc, char* argv[]) {

    if (argc != 4) {
        std::cerr << "Foloseste: " << argv[0] << " <num_mappers> <num_reducers> <input_file>" << std::endl;
        return EXIT_FAILURE;
    }

    int num_mappers = std::stoi(argv[1]);
    int num_reducers = std::stoi(argv[2]);
    std::string input_file = argv[3];

    FileQueue queue;
    init_queue(queue);

    std::ifstream fp(input_file);
    if (!fp) {
        std::cerr << "Fisierul nu a putut fi deschis." << std::endl;
        return EXIT_FAILURE;
    }

    int num_files;
    fp >> num_files;
    fp.ignore();
    for (int i = 0; i < num_files; i++) {
        std::string file;
        std::getline(fp, file);
        // Se pun fisierele in coada
        enqueue(queue, file);
    }
    fp.close();

    std::unordered_map<std::string, std::unordered_set<int>> results;
    pthread_mutex_t result_lock;
    pthread_mutex_init(&result_lock, NULL);
    pthread_barrier_t barrier;
    int total_threads = num_mappers + num_reducers;
    pthread_barrier_init(&barrier, NULL, total_threads);

    pthread_t threads[total_threads];
    for (int i = 0; i < total_threads; i++) {
        if (i < num_mappers) {
            MapperArgs* mapper_args = new MapperArgs{&queue, &results, &result_lock, &barrier, i};
            pthread_create(&threads[i], NULL, mapper_function, mapper_args);
        } else {
            ReducerArgs* reducer_args = new ReducerArgs{&results, &result_lock, &barrier, i, num_mappers, num_reducers};
            pthread_create(&threads[i], NULL, reducer_function, reducer_args);
        }
    }

    for (int i = 0; i < total_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&result_lock);
    pthread_barrier_destroy(&barrier);

    return EXIT_SUCCESS;
}


