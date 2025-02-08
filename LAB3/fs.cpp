#include <iostream>
#include "fs.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

std::vector<int> currentDir = {ROOT_BLOCK}; //global current directory tracker, start with root directory

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{
}

// formats the disk, i.e., creates an empty file system
int
FS::format()
{
   uint8_t empty[BLOCK_SIZE] = {0};
     // Initialize the FAT and clear blocks
    for (int i = 0; i < BLOCK_SIZE / 2; ++i) {
      this->disk.write(i, empty);
      this->fat[i] = FAT_FREE;
    }
    this->fat[0] = FAT_EOF;
    this->fat[1] = FAT_EOF;

    //Initialize the disk
    this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(this->fat)); //Write FAT to disk
    return 0;
}

//Finds next free block in the FAT
int 
FS::find_free_block() {
    uint8_t fat_read_buffer[BLOCK_SIZE] = {0}; // Read FAT from disk
    this->disk.read(FAT_BLOCK, fat_read_buffer);
    int16_t active_fat[BLOCK_SIZE / sizeof(int16_t)];
    std::memcpy(active_fat, fat_read_buffer, sizeof(active_fat));

    int free_index = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(int16_t); i++) {
        if (active_fat[i] == FAT_FREE) {
            free_index = i;
            return free_index; // Return immediately when a free block is found
        }
    }

    return -1; // No free block found
}
//Convert accessrights from uint8_t to string
std::string FS::access_rights_to_string(uint8_t accessrights){
  if(accessrights == 0x00){
      return "---";
    }
    else if(accessrights == EXECUTE){
      return "--x";
    }
    else if(accessrights == WRITE){
      return "-w-";
    }
    else if(accessrights == (EXECUTE | WRITE)){
      return "-wx";
    }
    else if(accessrights == READ){
      return "r--";
    }
    else if(accessrights == (READ | EXECUTE)){
      return "r-x";
    }
    else if(accessrights == (READ | WRITE)){
      return "rw-";
    }
    else if(accessrights == (READ | WRITE | EXECUTE)){
      return "rwx";
    }
  return "";
}

//Extract path name from an absolute or relative path
std::string FS::path_name(std::string path) {
    size_t pos = path.find_last_of('/'); //Find the last occurrence of '/'
    if (pos == std::string::npos) {
      return path;
    }
    return path.substr(pos + 1); //Return the substring after the last '/'
}


//load directory based on dirpath (std::string),loads the parent directory to the target
std::vector<int>
FS::find_disk_path(std::string dirpath){
    //break up dirpath to vector
    std::vector<std::string> destpath;
    std::stringstream ss(dirpath);
    std::string path;

    while(std::getline(ss, path, '/')){
      if(!path.empty()){
        destpath.push_back(path);
      }
    }
    int target_index = -2;
    std::vector<int> target_dir = {};
    if(destpath.size() == 1 && dirpath[0] != '/') {
        return currentDir;
    }
    if(destpath[0] == "."){
       target_index = currentDir.back();
       target_dir = currentDir;
    }
    else if(destpath[0] == ".."){ //handle relative starting path
       target_dir = currentDir;
    }
    else{ //handle absolute paths
      target_index = ROOT_BLOCK;
      target_dir = {ROOT_BLOCK};
    };

    //loop through to the target directory
    for(int i = 0; i < destpath.size()-1; i++){
        //jump back one step if /../
        if(destpath[i] == ".."){
            target_dir.pop_back();
            target_index = target_dir.back();
        }
        else{
            uint8_t read_buffer[BLOCK_SIZE] = {0}; // Buffer to read data from disk
            this->disk.read(target_index, read_buffer); // Read the block into the buffer
            // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
            dir_entry current_dir_loaded[BLOCK_SIZE / sizeof(dir_entry)];
            // Copy data from buffer to dir_entry array
            for (int j = 0; j < BLOCK_SIZE / sizeof(dir_entry); ++j) {
                std::memcpy(&current_dir_loaded[j], read_buffer + j * sizeof(dir_entry), sizeof(dir_entry));
            }
            for(int y = 0; y < BLOCK_SIZE / sizeof(dir_entry); y++){
              if(current_dir_loaded[y].file_name == destpath[i]){
                target_index = current_dir_loaded[y].first_blk;
                target_dir.push_back(current_dir_loaded[y].first_blk);
                break;
              }
            }
        }
    }
    return target_dir;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int 
FS::create(std::string filepath) {
    if (filepath.size() > 55) {
        std::cout << "File name too long" << std::endl;
        return -1;
    }

    dir_entry new_file{"", 0, 0, TYPE_FILE, READ | WRITE}; // Create new_file entry

    // Copy the file path to the file_name field
    for (int i = 0; i < filepath.size(); i++) { 
        new_file.file_name[i] = filepath[i];
        new_file.file_name[i + 1] = '\0';
    }

    //check if filename already exists
    uint8_t filename_read_buffer[BLOCK_SIZE] = {0};
    this->disk.read(find_disk_path(filepath).back(), filename_read_buffer);

    dir_entry filename_read_entries[BLOCK_SIZE / sizeof(dir_entry)];
    std::memcpy(filename_read_entries, filename_read_buffer, sizeof(filename_read_entries));
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) { 
        // Check if file name matches
        if (filename_read_entries[i].file_name == filepath) {
            std::cout << "File already exists!" << std::endl;
            return -1;
        }
    }
    std::string input; // user input
    std::vector<char> file; // user input vector
    std::vector<std::vector<char>> blocks;

    while (getline(std::cin, input) && !input.empty()) { // read user input for the file
        file.insert(file.end(), input.begin(), input.end()); // Insert input directly into file vector
        file.push_back('\n'); // add a newline after each input
    }

    new_file.size = file.size(); // byte size of file

    // Allocate and fill blocks with file data
    for (size_t i = 0; i < file.size(); i += BLOCK_SIZE) { //loop to handle large files
        std::vector<char> tempblock(BLOCK_SIZE, 0); // Create a temporary block
        size_t copy_size = std::min(BLOCK_SIZE, static_cast<int>(file.size() - i)); // Determine how much to copy
        std::copy(file.begin() + i, file.begin() + i + copy_size, tempblock.begin()); // Copy data to block
        blocks.push_back(tempblock); // Add the block to the list of blocks
    }
    // Find the first free block for the file
    uint16_t block_index = find_free_block(); // Using find_free_block to get the first block
    uint16_t first_block = block_index; // Store the first block index
    uint16_t previous_block_index = block_index; // Track the previous block index

    new_file.first_blk = block_index; // Set the first block in the new_file entry

    // Reading the FAT
    uint8_t fat_read_buffer[BLOCK_SIZE] = {0};
    this->disk.read(FAT_BLOCK, fat_read_buffer);
    int16_t active_fat[BLOCK_SIZE / sizeof(int16_t)];
    std::memcpy(active_fat, fat_read_buffer, sizeof(active_fat));

    // Writing the file blocks and updating FAT
    for (size_t i = 0; i < blocks.size(); ++i) { // loop to handle writing multiple blocks
        this->disk.write(block_index, reinterpret_cast<uint8_t*>(blocks[i].data())); // Write current block to disk

        if (i == blocks.size() - 1) { // If this is the last block
            active_fat[block_index] = FAT_EOF; // Mark the last block as EOF in FAT
        } else { 
            previous_block_index = block_index; // Save current block index
            block_index = find_free_block(); // Find the next free block
            active_fat[previous_block_index] = block_index; // Link the previous block to the new block in FAT
        }
    }
    this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(active_fat)); // Update the FAT on disk

    // Reading the directory
    uint8_t read_buffer[BLOCK_SIZE] = {0};
    this->disk.read(find_disk_path(filepath).back(), read_buffer);
    dir_entry large_read_dir_entry[BLOCK_SIZE / sizeof(dir_entry)];
    std::memcpy(large_read_dir_entry, read_buffer, sizeof(large_read_dir_entry)); // copy of directory

    int unused_index = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) { // Find the first free entry in directory
        if (large_read_dir_entry[i].file_name[0] == '\0' && large_read_dir_entry[i].first_blk == 0) {
            unused_index = i;
            break;
        }
    }
    if (unused_index == -1) {
        std::cout << "Problem with finding directory space!" << std::endl;
        return -1;
    }
    large_read_dir_entry[unused_index] = new_file;
    uint8_t write_buffer[BLOCK_SIZE] = {0};
    std::memcpy(write_buffer, large_read_dir_entry, sizeof(large_read_dir_entry)); //copying to write buffer
    this->disk.write(find_disk_path(filepath).back(), write_buffer); // Write the updated directory to disk

    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{   
    uint8_t read_buffer[BLOCK_SIZE] = {0};
    this->disk.read(find_disk_path(filepath).back(), read_buffer);
    dir_entry large_read_dir_entry[BLOCK_SIZE/sizeof(dir_entry)];
    // Loop through the read buffer and extract dir_entry structs
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        // Copy 64 bytes (size of dir_entry) from the read buffer into a dir_entry object
        std::memcpy(&large_read_dir_entry[i], read_buffer + i * sizeof(dir_entry), sizeof(dir_entry));
    }
    int filepathIndex = -1;

    //find first_blk
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); i++) {
        std::string currentFileName(large_read_dir_entry[i].file_name, strnlen(large_read_dir_entry[i].file_name, 56));
        if (currentFileName.compare(0, filepath.size(), filepath) == 0) {
            filepathIndex = large_read_dir_entry[i].first_blk;
            //check if file is type dir
            if (large_read_dir_entry[i].type == 1) {
                  std::cout << "Error! Can't read directory type!" << std::endl;
                  return -1;
            }
            //Check if file has read permissions
            if (large_read_dir_entry[i].access_rights != 0x04 && large_read_dir_entry[i].access_rights != 0x05 && large_read_dir_entry[i].access_rights != 0x06 && large_read_dir_entry[i].access_rights != 0x07) {
                  std::cout << "Error! No permissions!" << std::endl;
                  return -1;
            }
        }
    }    
    if(filepathIndex == -1){
        std::cout << "File not found!" << std::endl;
        return -1;
    }

    //fetch FAT
    uint8_t fat_read_buffer[BLOCK_SIZE] = {0};
    this->disk.read(FAT_BLOCK, fat_read_buffer);
    int16_t active_fat[BLOCK_SIZE / sizeof(int16_t)];
    std::memcpy(active_fat, fat_read_buffer, sizeof(active_fat));
    
    std::string file_content = "";
    char temp_file_content[BLOCK_SIZE];
    uint8_t temp_file_content_buffer[BLOCK_SIZE];

    //Read from FAT until EOF, store content
    while(filepathIndex != -1){
        this->disk.read(filepathIndex, temp_file_content_buffer);
        std::memcpy(temp_file_content,temp_file_content_buffer,BLOCK_SIZE);
        for(int i = 0; i < sizeof(temp_file_content);i++){
            file_content += temp_file_content[i];
        }
        filepathIndex = active_fat[filepathIndex];
    }
    std::cout << file_content << std::endl;
    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int 
FS::ls() {
    uint8_t read_buffer[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(currentDir.back(), read_buffer); // Read the  block into the buffer
    // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
    dir_entry large_read_dir_entry[BLOCK_SIZE / sizeof(dir_entry)];
    // Copy data from buffer to dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        std::memcpy(&large_read_dir_entry[i], read_buffer + i * sizeof(dir_entry), sizeof(dir_entry));
    }
    std::cout << "Name        type     accessrights    Size" << std::endl;
    // Iterate over the number of elements in the dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        // Check if the entry is valid (ex non-empty file_name)
        if (large_read_dir_entry[i].file_name[0] != '\0') {
            if(large_read_dir_entry[i].type == TYPE_FILE){
              std::cout << large_read_dir_entry[i].file_name << "          " << "file" << "        " << access_rights_to_string(large_read_dir_entry[i].access_rights) << "          "<<large_read_dir_entry[i].size << std::endl;
            }
            else{
              std::cout << large_read_dir_entry[i].file_name << "          " << "dir" << "         " << access_rights_to_string(large_read_dir_entry[i].access_rights) << "          " <<large_read_dir_entry[i].size << std::endl;
            }
        }
    }
    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    if (path_name(destpath).size() > 55) {
        std::cout << "File name too long" << std::endl;
        return -1;
    }
    //Fetch CURRENT PARENT DIRECTORY and make it readable
    uint8_t read_buffer1[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(find_disk_path(sourcepath).back(), read_buffer1); // Read the block into the buffer
    // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
    dir_entry source_parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    // Copy data from buffer to dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        std::memcpy(&source_parent_dir[i], read_buffer1 + i * sizeof(dir_entry), sizeof(dir_entry));
    }

    //Fetch DESTINATION PARENT DIRECTORY and make it readable
    uint8_t read_buffer2[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(find_disk_path(destpath).back(), read_buffer2); // Read the block into the buffer
    // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
    dir_entry destination_parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    // Copy data from buffer to dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        std::memcpy(&destination_parent_dir[i], read_buffer2 + i * sizeof(dir_entry), sizeof(dir_entry));
    }

	//check if DST already exists
    int destination_index = -1;

    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        // Check if file name matches
        if (std::string(destination_parent_dir[i].file_name) == path_name(destpath)) {
          if(destination_parent_dir[i].type == TYPE_FILE){
            std::cout << "File already exists!" << std::endl;
            return -1;
          }
          else{
            destination_index = i;
            break;
          }
        }
    }
   if(destpath == ".."){
      destination_index = currentDir[currentDir.size() - 2];
   }
    //Create new dir_entry
    dir_entry new_file{"", 0, 0, TYPE_FILE, READ | WRITE};

    // Iterate over the number of elements in the dir_entry array to find index of source.
    int source_index = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
      if(path_name(sourcepath) == source_parent_dir[i].file_name){
        source_index = i;
        break;
      }
    }
    //Error message incase source does not exist.
    if(source_index == -1){
      std::cout << "Source file not found!" << std::endl;
      return -1;
    }

	//write source attributes to destination
    new_file.size = source_parent_dir[source_index].size;
    new_file.type = source_parent_dir[source_index].type;
    new_file.access_rights = source_parent_dir[source_index].access_rights;

     //read FAT
    uint8_t fat_read_buffer[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(FAT_BLOCK, fat_read_buffer);
    int16_t active_fat[BLOCK_SIZE / sizeof(int16_t)];
    std::memcpy(active_fat, fat_read_buffer, sizeof(active_fat));

    std::vector <int> fat_indicies = {source_parent_dir[source_index].first_blk};
	//find all associated blocks
	while (active_fat[fat_indicies.back()] != -1){
        fat_indicies.push_back(active_fat[fat_indicies.back()]);
	}
	//copy source blocks to new blocks and fill FAT
	std::vector <int> free_blocks;
    for(int i = 0; i < fat_indicies.size(); i++){
		free_blocks.push_back(find_free_block());
        uint8_t read_buffer[BLOCK_SIZE];
        uint8_t read_data[BLOCK_SIZE];
        this->disk.read(fat_indicies[i], read_buffer);
        std::memcpy(read_data, read_buffer, sizeof(read_data));
		this->disk.write(free_blocks.back(),read_data);
        //write the first block to dir_entry
        if(i == 0){
          new_file.first_blk = free_blocks.back();
        }
        else{
          active_fat[free_blocks.back()-1] = free_blocks.back();
        }
    }
    active_fat[free_blocks.back()] = FAT_EOF;
	this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(active_fat));

    //DST is a DIR type, copy file to sub directory
    if (destination_index != -1){
      // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
      dir_entry destination_dir[BLOCK_SIZE / sizeof(dir_entry)];
      if(destpath == ".."){
        //Fetch DESTINATION DIRECTORY and make it readable
        uint8_t read_buffer3[BLOCK_SIZE] = {0}; // Buffer to read data from disk
        this->disk.read(destination_index, read_buffer3); // Read the block into the buffer
          // Copy data from buffer to dir_entry array
          for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
            std::memcpy(&destination_dir[i], read_buffer3 + i * sizeof(dir_entry), sizeof(dir_entry));
          }
       }
      else{
          //Fetch DESTINATION DIRECTORY and make it readable
          uint8_t read_buffer3[BLOCK_SIZE] = {0}; // Buffer to read data from disk
          this->disk.read(destination_parent_dir[destination_index].first_blk, read_buffer3); // Read the block into the buffer
          // Copy data from buffer to dir_entry array
          for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
            std::memcpy(&destination_dir[i], read_buffer3 + i * sizeof(dir_entry), sizeof(dir_entry));
          }
      }

      //Write name to dir_entry
      for (int i = 0; i < path_name(sourcepath).size(); i++) {
            new_file.file_name[i] = path_name(sourcepath)[i];
            new_file.file_name[i + 1] = '\0';
      }
      //find space in directory
        int unused_index = -1;
        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) { // Find the first free entry in directory
            if (destination_dir[i].file_name[0] == '\0' && destination_dir[i].first_blk == 0) {
                unused_index = i;
                break;
            }
        }
        if (unused_index == -1) {
            std::cout << "Problem with finding directory space!" << std::endl;
            return -1;
        }

        //write to the target_dir
        destination_dir[unused_index] = new_file; //write new to directory
        //write file to target directory
        uint8_t write_buffer[BLOCK_SIZE] = {0};
        std::memcpy(write_buffer, destination_dir, sizeof(destination_dir)); //copy to write buffer
        if(destpath == ".."){
           this->disk.write(destination_index, write_buffer); // Write the updated directory to disk
        }
        else{
           this->disk.write(destination_parent_dir[destination_index].first_blk, write_buffer); // Write the updated directory to disk
        }
    }

    //DST is a FILE type, make copy within same directory
    else{
        //check if DST already exists
        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
            // Check if file name matches
            if (std::string(source_parent_dir[i].file_name) == path_name(destpath)) {
              if(source_parent_dir[i].type == TYPE_FILE){
                std::cout << "File already exists!" << std::endl;
                return -1;
              }
            }
        }
        int unused_index = -1;
        //Write name to dir_entry
        for (int i = 0; i < path_name(destpath).size(); i++) {
            new_file.file_name[i] = path_name(destpath)[i];
            new_file.file_name[i + 1] = '\0';
        }
        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) { // Find the first free entry in directory
            if (source_parent_dir[i].file_name[0] == '\0' && source_parent_dir[i].first_blk == 0) {
                unused_index = i;
                break;
            }
        }
        if (unused_index == -1) {
            std::cout << "Problem with finding directory space!" << std::endl;
            return -1;
        }

        source_parent_dir[unused_index] = new_file; //write new to directory
        uint8_t write_buffer[BLOCK_SIZE] = {0};
        std::memcpy(write_buffer, source_parent_dir, sizeof(source_parent_dir));
        this->disk.write(find_disk_path(sourcepath).back(), write_buffer); // Write the updated directory to disk
    }
    return 0;
}


// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    if (path_name(destpath).size() > 55) {
        std::cout << "File name too long" << std::endl;
        return -1;
    }
    //Fetch SOURCE PARENT DIRECTORY and make it readable
    uint8_t read_buffer1[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(find_disk_path(sourcepath).back(), read_buffer1); // Read the block into the buffer
    // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
    dir_entry source_parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    // Copy data from buffer to dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        std::memcpy(&source_parent_dir[i], read_buffer1 + i * sizeof(dir_entry), sizeof(dir_entry));
    }

    //Fetch DESTINATION PARENT DIRECTORY and make it readable
    uint8_t read_buffer2[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(find_disk_path(destpath).back(), read_buffer2); // Read the block into the buffer
    // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
    dir_entry destination_parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    // Copy data from buffer to dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        std::memcpy(&destination_parent_dir[i], read_buffer2 + i * sizeof(dir_entry), sizeof(dir_entry));
    }

    //find source file index
    int source_index = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
      if(path_name(sourcepath) == source_parent_dir[i].file_name){
        source_index = i;
        break;
      }
    }
    //Error message incase source does not exist.
    if(source_index == -1){
      std::cout << "Source file not found!" << std::endl;
      return -1;
    }

	//check if DST already exists and if type FILE or DIR
    int destination_index = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        // Check if file name matches
        if (std::string(destination_parent_dir[i].file_name) == path_name(destpath)) {
          if(destination_parent_dir[i].type == TYPE_FILE){
            std::cout << "File already exists!" << std::endl;
            return -1;
          }
          else{
            destination_index = i;
            break;
          }
        }
    }
    if(destpath == ".."){
      destination_index = currentDir[currentDir.size() - 2];
   }
    //DST is a DIR type, move file to sub directory
    if (destination_index != -1){
       // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
       dir_entry destination_dir[BLOCK_SIZE / sizeof(dir_entry)];
       if(destpath == ".."){
         //Fetch DESTINATION DIRECTORY and make it readable
        uint8_t read_buffer2[BLOCK_SIZE] = {0}; // Buffer to read data from disk
        this->disk.read(destination_index, read_buffer2); // Read the block into the buffer
        // Copy data from buffer to dir_entry array
        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
            std::memcpy(&destination_dir[i], read_buffer2 + i * sizeof(dir_entry), sizeof(dir_entry));
        }
       }
       else{
         //Fetch DESTINATION DIRECTORY and make it readable
        uint8_t read_buffer2[BLOCK_SIZE] = {0}; // Buffer to read data from disk
        this->disk.read(destination_parent_dir[destination_index].first_blk, read_buffer2); // Read the block into the buffer
        // Copy data from buffer to dir_entry array
        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
            std::memcpy(&destination_dir[i], read_buffer2 + i * sizeof(dir_entry), sizeof(dir_entry));
        }
       }
         //find space in target directory
        int unused_index = -1;
        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) { // Find the first free entry in directory
            if (destination_dir[i].file_name[0] == '\0' && destination_dir[i].first_blk == 0) {
                unused_index = i;
                break;
            }
        }
        if (unused_index == -1) {
            std::cout << "Problem with finding directory space!" << std::endl;
            return -1;
        }
        //write file to target directory
         destination_dir[unused_index] = source_parent_dir[source_index];
         uint8_t write_buffer1[BLOCK_SIZE] = {0};
        std::memcpy(write_buffer1, destination_dir, sizeof(destination_dir)); //copy to write buffer
        if(destpath == ".."){
           this->disk.write(destination_index, write_buffer1); // Write the updated directory to disk
        }
       else{
         this->disk.write(destination_parent_dir[destination_index].first_blk, write_buffer1); // Write the updated directory to disk
       }

        //remove file from source directory
        std::memset(&source_parent_dir[source_index], 0, sizeof(dir_entry));
        uint8_t write_buffer2[BLOCK_SIZE] = {0};
        std::memcpy(write_buffer2, source_parent_dir, sizeof(source_parent_dir));
        this->disk.write(find_disk_path(sourcepath).back(), write_buffer2); // Write the updated directory to disk
      return 0;
    }
    //DST is a FILE type, rename file
    else{
         //Clear old filename
        for(int i = 0; i < path_name(sourcepath).size(); i++){
          source_parent_dir[source_index].file_name[i] = '\0';
        }
        // Copy the file path to the file_name field
        for (int i = 0; i < path_name(destpath).size(); i++) {
            source_parent_dir[source_index].file_name[i] = destpath[i];
            source_parent_dir[source_index].file_name[i + 1] = '\0';
        }
        uint8_t write_buffer[BLOCK_SIZE] = {0};
        std::memcpy(write_buffer, source_parent_dir, sizeof(source_parent_dir));
        this->disk.write(find_disk_path(sourcepath).back(), write_buffer); // Write the updated directory to disk

        return 0;
    }
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    //Fetch directory and make it readable
    uint8_t read_buffer[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(find_disk_path(filepath).back(), read_buffer); // Read the block into the buffer
    // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
    dir_entry large_read_dir_entry[BLOCK_SIZE / sizeof(dir_entry)];
    // Copy data from buffer to dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        std::memcpy(&large_read_dir_entry[i], read_buffer + i * sizeof(dir_entry), sizeof(dir_entry));
    }

    //find source index of dir_entry
    int source_index = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
      if(path_name(filepath) == large_read_dir_entry[i].file_name){
        source_index = i;
        break;
      }
    }
    //Error message incase source does not exist.
    if(source_index == -1){
      std::cout << "File not found!" << std::endl;
      return -1;
    }

    //Check if dir type
    if(large_read_dir_entry[source_index].type == TYPE_DIR){
        //Fetch dir and make it readable
        uint8_t read_buffer2[BLOCK_SIZE] = {0}; // Buffer to read data from disk
        this->disk.read(large_read_dir_entry[source_index].first_blk, read_buffer); // Read the dir block into the buffer
        // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
        dir_entry large_read_dir_entry2[BLOCK_SIZE / sizeof(dir_entry)];
        // Copy data from buffer to dir_entry array
        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
            std::memcpy(&large_read_dir_entry2[i], read_buffer2 + i * sizeof(dir_entry), sizeof(dir_entry));
        }
        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
          if(large_read_dir_entry2[i].file_name[0] != '\0'){
            std::cout << "Directory has dependencies, cannot remove!" << std::endl;
            return -1;
          }
        }
    }

	//remove FAT and disk entries

    //read FAT
    uint8_t fat_read_buffer[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(FAT_BLOCK, fat_read_buffer);
    int16_t active_fat[BLOCK_SIZE / sizeof(int16_t)];
    std::memcpy(active_fat, fat_read_buffer, sizeof(active_fat));
    int current_fat = large_read_dir_entry[source_index].first_blk;
    int next_fat = -1;

    while(current_fat != 0) {
      next_fat = active_fat[current_fat];
      active_fat[current_fat] = FAT_FREE;
      current_fat = next_fat;
    }

    this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(active_fat)); // Update the FAT on disk


	//Clear file from directory
	std::memset(&large_read_dir_entry[source_index], 0, sizeof(dir_entry));
	uint8_t write_buffer[BLOCK_SIZE] = {0};
	std::memcpy(write_buffer, large_read_dir_entry, sizeof(large_read_dir_entry));
	this->disk.write(find_disk_path(filepath).back(), write_buffer); // Write the updated directory to disk



    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    //Fetch CURRENT PARENT DIRECTORY and make it readable
    uint8_t read_buffer1[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(find_disk_path(filepath1).back(), read_buffer1); // Read the block into the buffer
    // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
    dir_entry filepath1_parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    // Copy data from buffer to dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        std::memcpy(&filepath1_parent_dir[i], read_buffer1 + i * sizeof(dir_entry), sizeof(dir_entry));
    }

    //Fetch DESTINATION PARENT DIRECTORY and make it readable
    uint8_t read_buffer2[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(find_disk_path(filepath2).back(), read_buffer2); // Read the block into the buffer
    // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
    dir_entry filepath2_parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    // Copy data from buffer to dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        std::memcpy(&filepath2_parent_dir[i], read_buffer2 + i * sizeof(dir_entry), sizeof(dir_entry));
    }
    //check if file1 and file2 exist
    int file_index_1, file_index_2 = -2;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
      if(filepath1_parent_dir[i].file_name == filepath1){
        file_index_1 = i;
        //Check if file has read permissions
            if (filepath1_parent_dir[i].access_rights != 0x04 && filepath1_parent_dir[i].access_rights != 0x05 && filepath1_parent_dir[i].access_rights != 0x06 && filepath1_parent_dir[i].access_rights != 0x07) {
                  std::cout << "Error! No permissions!" << std::endl;
                  return -1;
            }
      }
      if(filepath2_parent_dir[i].file_name == filepath2){
        file_index_2 = i;
        //Check if file has write permissions
            if (filepath1_parent_dir[i].access_rights != 0x02 && filepath1_parent_dir[i].access_rights != 0x03 && filepath1_parent_dir[i].access_rights != 0x06 && filepath1_parent_dir[i].access_rights != 0x07) {
                  std::cout << "Error! No permissions!" << std::endl;
                  return -1;
            }
      }
    }
    //return if either doesnt exist
    if(file_index_1 == -2 || file_index_2 == -2){
      std::cout << "File not found!" << std::endl;
      return -1;
    }
    //update size of dir_entry
    filepath2_parent_dir[file_index_2].size += filepath1_parent_dir[file_index_1].size;

    uint8_t write_buffer[BLOCK_SIZE] = {0};
    std::memcpy(write_buffer, filepath2_parent_dir, sizeof(filepath2_parent_dir));
	this->disk.write(find_disk_path(filepath2).back(), write_buffer); // Write the updated directory to disk



     //read FAT
    uint8_t fat_read_buffer[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(FAT_BLOCK, fat_read_buffer);
    int16_t active_fat[BLOCK_SIZE / sizeof(int16_t)];
    std::memcpy(active_fat, fat_read_buffer, sizeof(active_fat));

    std::vector <int> fat_indicies = {filepath1_parent_dir[file_index_1].first_blk};
	//find all associated blocks
	while (active_fat[fat_indicies.back()] != -1){
        fat_indicies.push_back(active_fat[fat_indicies.back()]);
	}
	//copy source blocks to new blocks and fill FAT
	std::vector <int> free_blocks;
    for(int i = 0; i < fat_indicies.size(); i++){
		free_blocks.push_back(find_free_block());
        uint8_t read_buffer[BLOCK_SIZE];
        uint8_t read_data[BLOCK_SIZE];
        this->disk.read(fat_indicies[i], read_buffer);
        std::memcpy(read_data, read_buffer, sizeof(read_data));
		this->disk.write(free_blocks.back(),read_data);
        //write the first block to dir_entry
        if(i != 0){
          active_fat[free_blocks.back()-1] = free_blocks.back();
        }
    }
    active_fat[free_blocks.back()] = FAT_EOF;

        //find EOF file2
    int currentFATval = filepath2_parent_dir[file_index_2].first_blk;
    while (active_fat[currentFATval] != -1) {
        currentFATval = active_fat[currentFATval];
    }
    //append first block of new chain to EOF file2
    active_fat[currentFATval] = free_blocks[0];
	this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(active_fat));

    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    if (path_name(dirpath).size() > 55) {
        std::cout << "Directory name too long!" << std::endl;
        return -1;
    }
    //Fetch directory and make it readable
    uint8_t read_buffer[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(find_disk_path(dirpath).back(), read_buffer); // Read the block into the buffer
    // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
    dir_entry destination_parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    // Copy data from buffer to dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        std::memcpy(&destination_parent_dir[i], read_buffer + i * sizeof(dir_entry), sizeof(dir_entry));
    }

    //check if DST already exists
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        // Check if file name matches
        if (std::string(destination_parent_dir[i].file_name) == path_name(dirpath)) {
            std::cout << "File already exists!" << std::endl;
            return -1;
        }
    }
    //new dir_entry
    dir_entry new_dir{"", 0, 0, TYPE_DIR, READ | WRITE};
    //first blk
    uint16_t block_index = find_free_block();
    new_dir.first_blk = block_index;
    // Copy the file path to the file_name field
    for (int i = 0; i < path_name(dirpath).size(); i++) {
        new_dir.file_name[i] = path_name(dirpath)[i];
        new_dir.file_name[i + 1] = '\0';
    }
    //save dir to currentDir
    int unused_index = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) { // Find the first free entry in directory
        if (destination_parent_dir[i].file_name[0] == '\0' && destination_parent_dir[i].first_blk == 0) {
            unused_index = i;
            break;
        }
    }
    if (unused_index == -1) {
        std::cout << "Problem with finding directory space!" << std::endl;
        return -1;
    }
    destination_parent_dir[unused_index] = new_dir;
    uint8_t write_buffer[BLOCK_SIZE] = {0};
    std::memcpy(write_buffer, destination_parent_dir, sizeof(destination_parent_dir)); //copy to write buffer
    this->disk.write(find_disk_path(dirpath).back(), write_buffer); // Write the updated directory to disk

    // Reading the FAT
    uint8_t fat_read_buffer[BLOCK_SIZE] = {0};
    this->disk.read(FAT_BLOCK, fat_read_buffer);
    int16_t active_fat[BLOCK_SIZE / sizeof(int16_t)];
    std::memcpy(active_fat, fat_read_buffer, sizeof(active_fat));
    //save in FAT
    active_fat[block_index] = FAT_EOF;
    //push FAT
    this->disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(active_fat)); // Update the FAT on disk

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    if(dirpath.empty()){
      currentDir = {ROOT_BLOCK};
      return 0;
    }
    else if(dirpath == ".."){
      currentDir.pop_back();
      return 0;
    }
    //cd based on absolute path
    else if(dirpath[0] == '/'){
      currentDir = {ROOT_BLOCK};
      std::vector<int> path = find_disk_path(dirpath);
      for(int i = 1; i < path.size(); i++){
        currentDir.push_back(path[i]);
      }
      // read last value and push_back that
      dir_entry destination_parent_path[BLOCK_SIZE / sizeof(dir_entry)];
      //Fetch directory and make it readable
      uint8_t read_buffer[BLOCK_SIZE] = {0}; // Buffer to read data from disk
       this->disk.read(find_disk_path(dirpath).back(), read_buffer); // Read the block into the buffer
      // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
      // Copy data from buffer to dir_entry array
      for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
          std::memcpy(&destination_parent_path[i], read_buffer + i * sizeof(dir_entry), sizeof(dir_entry));
      }
       for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
          if(destination_parent_path[i].file_name == path_name(dirpath)){
            currentDir.push_back(destination_parent_path[i].first_blk);
          }
       }

      return 0;
    }
    else{
        dir_entry destination_parent_path[BLOCK_SIZE / sizeof(dir_entry)];
        //Fetch directory and make it readable
        uint8_t read_buffer[BLOCK_SIZE] = {0}; // Buffer to read data from disk
        this->disk.read(find_disk_path(dirpath).back(), read_buffer); // Read the block into the buffer
        // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
        // Copy data from buffer to dir_entry array
        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
            std::memcpy(&destination_parent_path[i], read_buffer + i * sizeof(dir_entry), sizeof(dir_entry));
        }
        //Check target directory exists in working directory
        for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
          if(destination_parent_path[i].file_name == path_name(dirpath)){
            if(destination_parent_path[i].type == TYPE_DIR){
              std::vector<int> path = find_disk_path(dirpath);
                //remove already existing values
              for(int y = 0; y < path.size(); y++){
                if(path[y] == currentDir.back()){
                  path.erase(path.begin(), path.begin() + y + 1);
                  break;
                }
              }
                //append new values
              for(int j = 0; j < path.size(); j++){
                currentDir.push_back(path[j]);
              }
              //append destination
              currentDir.push_back(destination_parent_path[i].first_blk); //change directory
              return 0;
            }
          }
        }
      std::cout << "No directory found!" << std::endl;
      return -1;
    }

}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::string current_path = "/";
    for(int i = 1; i < currentDir.size(); i++){
        uint8_t read_buffer[BLOCK_SIZE] = {0}; // Buffer to read data from disk
        this->disk.read(currentDir[i-1], read_buffer); // Read the block into the buffer
        // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
        dir_entry large_read_dir_entry[BLOCK_SIZE / sizeof(dir_entry)];
        // Copy data from buffer to dir_entry array
         for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
            std::memcpy(&large_read_dir_entry[i], read_buffer + i * sizeof(dir_entry), sizeof(dir_entry));
        }
         for(int y = 0; y < BLOCK_SIZE / sizeof(dir_entry); y++){
           if(large_read_dir_entry[y].first_blk == currentDir[i]){
             current_path = current_path + large_read_dir_entry[y].file_name + "/";
             break;
           }
         }
    }
    std::cout << current_path << std::endl;
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    //Fetch CURRENT PARENT DIRECTORY and make it readable
    uint8_t read_buffer1[BLOCK_SIZE] = {0}; // Buffer to read data from disk
    this->disk.read(find_disk_path(filepath).back(), read_buffer1); // Read the block into the buffer
    // Calculate the number of directory entries based on BLOCK_SIZE and dir_entry size
    dir_entry source_parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    // Copy data from buffer to dir_entry array
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        std::memcpy(&source_parent_dir[i], read_buffer1 + i * sizeof(dir_entry), sizeof(dir_entry));
    }
    // Iterate over the number of elements in the dir_entry array to find index of source.
    int source_index = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
      if(path_name(filepath) == source_parent_dir[i].file_name){
        source_index = i;
        break;
      }
    }
    //Error message incase source does not exist.
    if(source_index == -1){
      std::cout << "Source file not found!" << std::endl;
      return -1;
    }
    if(accessrights == "0"){
      source_parent_dir[source_index].access_rights = 0x00;
    }
    else if(accessrights == "1"){
      source_parent_dir[source_index].access_rights = EXECUTE;
    }
    else if(accessrights == "2"){
      source_parent_dir[source_index].access_rights = WRITE;
    }
    else if(accessrights == "3"){
      source_parent_dir[source_index].access_rights = EXECUTE | WRITE;
    }
    else if(accessrights == "4"){
      source_parent_dir[source_index].access_rights = READ;
    }
    else if(accessrights == "5"){
      source_parent_dir[source_index].access_rights = READ | EXECUTE;
    }
    else if(accessrights == "6"){
      source_parent_dir[source_index].access_rights = READ | WRITE;
    }
    else if(accessrights == "7"){
      source_parent_dir[source_index].access_rights = READ | WRITE | EXECUTE;
    }
    else{
      std::cout << "Access Right not supported!" << std::endl;
      return -1;
    }
    //write file to target directory
    uint8_t write_buffer[BLOCK_SIZE] = {0};
    std::memcpy(write_buffer, source_parent_dir, sizeof(source_parent_dir)); //copy to write buffer
    this->disk.write(find_disk_path(filepath).back(), write_buffer); // Write the updated directory to disk

    return 0;
}