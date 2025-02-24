# Map-Reduce
For managing files for the Mapper, a file queue was used.  
Thus, a structure was utilized containing the file queue, a mutex for synchronization, and a unique file ID.

### Mapper
- A file queue was required. Additionally, each word, along with the list of files in which it appears, was stored using an `unordered_map`. A `set` was used to keep unique files.  
- A mutex was necessary for accessing the results in the `unordered_map` that contains all found words.  
- A barrier was used to synchronize the mapper and reducer.  
- The mapper's ID was stored in `thread_id`.

### Reducer
- In addition to the previous requirements, the total number of mappers and reducers was needed to correctly calculate each ID.

### Functions
- The `valid_word` function was used to transform each letter into lowercase format and remove non-alphabetic characters.
- The `init_queue`, `enqueue`, and `dequeue` functions were used for managing the file queue.
- The `compare_words` function was used to sort words based on the size of the set containing file IDs. Words that appear in more files are prioritized.

### Mapper Function
- A local `unordered_map` was created for each Mapper to store words and their corresponding file IDs.  
- To extract a file from the queue, `dequeue` was used, which also returns its ID.  
- The file was opened, and each word in the file was read. Non-alphabetic characters were removed, and the word, along with the file ID in which it was found, was added to the local result.  

Each word, along with the set of files in which it appears, was copied into the final `unordered_map`.  
To avoid concurrent writes, a mutex was used to protect access.  

A barrier was used to ensure that all Mapper operations were completed before starting the Reducer operations.

### Reducer Function
- A barrier was used to start operations only after all Mapper operations were completed.  
- Each reducer received a specific range of letters. The 26 letters were distributed among the Reducers.  
- If the distribution was not uniform, the remaining letters had to be distributed among the Reducers. Thus, a check was performed to determine if the current Reducer would receive an additional letter.  
- The start and end indices for the letter range were calculated. If the Reducer received an extra letter, the letter range was increased. If no additional letters were received, to calculate the start index, the already distributed extra letters had to be taken into account.

A vector was used to extract only the words that started with the specific letters assigned to each Reducer.  
The `unordered_map` containing all words was traversed, and words starting with the desired letters were stored in the vector.  
Words were sorted based on the number of files in which they appeared, prioritizing those appearing in more files.  

For each letter, a file of the format `letter.txt` was created.  
For each letter, the file IDs in which they appeared were written. Additionally, the file IDs were sorted in ascending order.

### Main Function
- In the `main` function, `M + R` threads were created and joined.
