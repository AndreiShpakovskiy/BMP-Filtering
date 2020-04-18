#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>

#define DEFAULT_BMP_SOURCE_DIRECTORY "/your_source_directory_path"
#define DEFAULT_BMP_DESTINATION_DIRECTORY "/your_destination_directory_path"

#define PIPE_READ_INDEX 0
#define PIPE_WRITE_INDEX 1

#define RED 0
#define GREEN 1
#define BLUE 2

//===== Procedures to work with raw BMP data
int getBMPDataOffset(char* imageData) {
    return *((int*) &imageData[0x0A]);
}

int getBMPWidth(char* imageData) {
    return *((int*) &imageData[0x12]);
}

int getBMPHeight(char* imageData) {
    return *((int*) &imageData[0x16]);
}

//Procedure receives the pointer to the file content, the number of color to be leaved untouched
//and the value to replace other two color with.
void leaveBMP24Color(char* imageData, int colorToLeave, char replacement) {
    int dataOffset = getBMPDataOffset(imageData);
    printf("Offset: %d\n", dataOffset);
    int pictureWidth = getBMPWidth(imageData);
    printf("Width: %d\n", pictureWidth);
    int pictureHeight = getBMPHeight(imageData);
    printf("Height: %d\n", pictureHeight);
    while (pictureWidth % 4) { //Increment the length of the image while it doesn't match "real" one.
        pictureWidth++;
    }
    long bytesToReplace = pictureHeight * pictureWidth; //Count the number of pixels to be edited.
    for (long i = dataOffset; i < bytesToReplace * 3 + dataOffset; i += 3) { //Don't forget about the offset.
        switch (colorToLeave) {
            case RED:
                imageData[i] = replacement;
                imageData[i + 1] = replacement;
                break;
            case BLUE:
                imageData[i + 1] = replacement;
                imageData[i + 2] = replacement;
                break;
            case GREEN:
                imageData[i] = replacement;
                imageData[i + 2] = replacement;
                break;
        }
    }
}

//===== Procedures to work with files

int getFileSizeByName(char* fileName) {
    FILE* fileHandle = fopen(fileName, "r");
    if (fileHandle) {
        fseek(fileHandle, 0L, SEEK_END);
        int fileSize = ftell(fileHandle);
        fclose(fileHandle);
        return fileSize;
    }
    else {
        printf("Size of file cannot be defined\n");
        return -1;
    }
}

int getFilesNumber(char* dirPath) {
    int dirFiles = 0;
    DIR* directory = opendir(dirPath);
    if (directory) {
        while (readdir(directory)) {
            dirFiles++;
        }
        closedir(directory);
        return dirFiles;
    }
    else {
        printf("Directory opening error");
    }
    return 0;
}

//===== Procedures to work with file paths

char** newFileNamesArray(int filesInDirectory) {
    char** filenamesArray;
    filenamesArray = (char**) calloc(sizeof(char*), filesInDirectory);
    for (int i = 0; i < filesInDirectory; i++) {
        filenamesArray[i] = (char*) calloc(sizeof(char), 256);
    }
    return filenamesArray;
}

char** getBMPPathsByDirectory(char* directoryPath, int* filesNumberPtr) {
    struct dirent* sourceDirStruct;
    DIR* sourceDir = opendir(directoryPath);
    if (sourceDir) {
        char** sourceDirFiles = newFileNamesArray(getFilesNumber(directoryPath) - 2);
        *filesNumberPtr = 0;
        while ((sourceDirStruct = readdir(sourceDir)) != NULL) {
            if (strstr(sourceDirStruct->d_name, "bmp")) {
                strcpy(sourceDirFiles[*filesNumberPtr], directoryPath);
                strcat(sourceDirFiles[*filesNumberPtr], "/");
                strcat(sourceDirFiles[*filesNumberPtr], sourceDirStruct->d_name);
                (*filesNumberPtr)++;
            }
        }
        closedir(sourceDir);
        return sourceDirFiles;
    }
    else {
        printf("Incorrect directory path\n");
    }
    (*filesNumberPtr) = -1;
    return NULL;
}

int main(int argc, char *argv[]) {
    //Get paths to BMP files according to source directory and count their number
    int filesToEdit;
    char** bmpPaths = getBMPPathsByDirectory(DEFAULT_BMP_SOURCE_DIRECTORY, &filesToEdit);

    if (bmpPaths != NULL) {
        //Create the process for every file
        for (int i = 0; i < filesToEdit; i++) {
            printf("Current file: %s\n", bmpPaths[i]);

            //Creare pipe
            int pipeHandles[2];
            if (pipe(pipeHandles) == -1) {
                printf("Pipe creating error\n");
                exit(EXIT_FAILURE);
            }

            //Create the process
            pid_t colorLeavingProcess = fork();
            if (colorLeavingProcess == -1) {
                printf("Process creating error\n");
                exit(EXIT_FAILURE);
            }

            if (colorLeavingProcess == 0) {
                close(pipeHandles[PIPE_WRITE_INDEX]);   //Close unused pipe's write end.
                int firstIteration = 1;                 //This value used as the flag of the first loop iteration.
                int bytesToRead = sizeof(int);          //This value used to define the number of bytes that have to be read
                                                        //during the particular(!) loop iteration.
                                                        //As we read from pipe in a single loop, the number of bytes to read and the
                                                        //pointer to the destination buffer have to be changed in loop's body.
                char* fileContent;                      //The pointer to the buffer, holding the whole file content.
                int fileSize;                           //Size of the file to be read.
                char* placeToPastePointer = (char*) &fileSize;//Here is the pointer to the current active destination buffer.
                while (read(pipeHandles[PIPE_READ_INDEX], placeToPastePointer, bytesToRead)) {
                    if (firstIteration) {               //The first 4 bytes of pipe content is nothing but size of file
                        firstIteration = 0;
                        printf("Size of file: %d\n", fileSize);
                        fileContent = (char*) malloc(fileSize); //Allocating the memory for file content
                        placeToPastePointer = fileContent;      //Changing destination buffer
                        bytesToRead = 1;                        //and number of bytes to read
                    }
                    else {
                        placeToPastePointer++;
                    }
                }

                leaveBMP24Color(fileContent, GREEN, 0x00);      //Editing the BMP file

                char savePath[256] = { 0 };                     //Generating the path of edited file
                strcpy(savePath, DEFAULT_BMP_DESTINATION_DIRECTORY);
                strcat(savePath, "/");
                sprintf(&savePath[strlen(savePath)], "%d", i);
                strcat(savePath, ".bmp");

                printf("Saving path: %s\n", savePath);

                FILE* updatedFile = fopen(savePath, "w");       //Saving the file
                fwrite(fileContent, fileSize, 1, updatedFile);
                fclose(updatedFile);

                free(fileContent);
                close(pipeHandles[PIPE_READ_INDEX]);
                _exit(EXIT_SUCCESS);
            }
            else {
                close(pipeHandles[PIPE_READ_INDEX]);                            //Close unused pipe's read end.
                int fileSize = getFileSizeByName(bmpPaths[i]);
                printf("Real size of file: %d\n", fileSize);
                FILE* fileHandle = fopen(bmpPaths[i], "r");
                if (fileHandle) {
                    write(pipeHandles[PIPE_WRITE_INDEX], &fileSize, sizeof(int));   //Write the size of file to the pipe
                    char* fileContent = malloc(fileSize);                           //Allocate memory
                    fread(fileContent, fileSize, 1, fileHandle);                    //Read the file
                    write(pipeHandles[PIPE_WRITE_INDEX], fileContent, fileSize);    //And write it to the pipe
                    close(pipeHandles[PIPE_WRITE_INDEX]);                           //Closing and freeing resources
                    fclose(fileHandle);
                    free(fileContent);
                }
                else {
                    printf("Error opening file");
                }
                wait(NULL);
            }
        }
    }
}
