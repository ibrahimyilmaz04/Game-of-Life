#include "mpi.h"
#include <omp.h>
#include <iostream>
#include <fstream>
#include <stdlib.h>

#define MASTER 0

using namespace std;

int* allocateMemory(int x, int y);
int* populate(int* mat, int x, int y);
void display(int* cells, int x, int y);
int* modify(int* cells, int rows, int columns);
int findNumberOfAliveNeighbours(int rowIndex, int columnIndex, int rows, int columns, int* cells);
void copyCells(int* source, int* destination, int sourceRows, int columns);
void updateCells(int start,int end, int columns, int* cells,int upperNeighbor, int lowerNeighbor,int rank, MPI_Status&, int numtasks, int threads);
int* populate(int row, int column, ifstream& inFile);
void print(int* mat,int x, int y);
void writeToFile(int* mat, int row, int column, char* outputFile);

int main(int argc, char* argv[]){
	char* inputFile = argv[1];
	int threads = atoi(argv[2]);
	int generations = atoi(argv[3]);
	char* outputFile = argv[4];
	int dumpPeriod;
	if(argc>5){
		dumpPeriod = atoi(argv[5]);
	}

	ifstream inFile;
	inFile.open(inputFile);
	if(inFile.fail()){
		cout<<"FILE not found. Try again "<<endl;
		return 1;
	}
	string dimension, values;
	getline(inFile, dimension);

	string temprow = "";
	string tempcol = "";
	bool spaceEncountered = false;
	for(int i=0; i<dimension.length(); i++){
		if(dimension[i] == ' '){
			spaceEncountered = true;
			continue;
		}
		if(!spaceEncountered){
			temprow += dimension[i];
		}else{
			tempcol += dimension[i];
		}
	}
	int rows = atoi(temprow.c_str());
	int columns = atoi(tempcol.c_str());

	int numtasks, rank;
   	MPI_Status status;
	MPI_Init(&argc,&argv);
	MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	char processor_name[MPI_MAX_PROCESSOR_NAME];
	int name_len;
	MPI_Get_processor_name(processor_name, &name_len);

	int rowPerThread = rows/numtasks;
	int remainder = rows%numtasks;

	int start, end;
	start = rank * rowPerThread;
	if(rank == numtasks - 1){
		end = rows;
	}else{
		end = start+rowPerThread;
	}

	int* initials;
	int* cells;
	
	double startTime;

	if(rank == MASTER){
		initials = populate(rows, columns, inFile);
		if(argc>5){
			writeToFile(initials, rows, columns, outputFile);
		}
		startTime = MPI_Wtime();
		for(int i = 1; i < numtasks; i++){
			int st = i * rowPerThread;
			int last;
			if(i == numtasks-1){
				last = rows;
				MPI_Send(&initials[(st-1)*columns], (last-st+1)*columns, MPI_INT, i, 0, MPI_COMM_WORLD);
				MPI_Send(&initials[0], columns, MPI_INT, i, 0, MPI_COMM_WORLD);
			}else{
				last = st+rowPerThread;
				MPI_Send(&initials[(st-1)*columns], (last-st+2)*columns, MPI_INT, i, 0, MPI_COMM_WORLD);
			}
		}
		cells = allocateMemory(end-start+2, columns);
		for(int i=0; i<columns; i++){
			cells[i] = initials[(rows-1)*columns+i];
		}
		for(int i = 0; i < (end-start+1)*columns; i++ ){
			cells[columns+i] = initials[i];
		}

	} else if (rank == numtasks-1){
		cells = allocateMemory(end-start+2, columns);
		MPI_Recv(&cells[0], (end-start+1)*columns, MPI_INT, 0, 0 , MPI_COMM_WORLD, &status);
		MPI_Recv(&cells[(end-start+1)*columns],columns, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);

	} else{	
		cells = allocateMemory(end-start+2, columns);
		MPI_Recv(&cells[0], (end-start+2)*columns, MPI_INT, 0, 0 , MPI_COMM_WORLD, &status);
	}

	int upperNeighbor = (rank == 0) ? numtasks-1 : rank-1;
	int lowerNeighbor = (rank == numtasks-1) ? 0 : rank+1;

	for(int i = 0; i<generations; i++){
        updateCells(start, end, columns, cells, upperNeighbor, lowerNeighbor, rank, status,numtasks, threads);
		if(argc>5 && i%dumpPeriod == 0){
			if(rank == MASTER){
				for(int i = 1; i< numtasks; i++){
					int st = i * rowPerThread;
					int last;
					if(i == numtasks-1){
						last = rows;
					}else{
						last = st+rowPerThread;
					}
					MPI_Recv(&initials[(st)*columns], (last-st)*columns, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
				}
				for(int i = 0; i < (end-start)*columns; i++ ){
					initials[i] = cells[columns+i];
				}
				writeToFile(initials, rows, columns, outputFile);
			}else{
				MPI_Send(&cells[columns], (end-start)*columns, MPI_INT, 0, 0 , MPI_COMM_WORLD);
			}
		}
	}

	if(rank == MASTER){
		for(int i = 1; i< numtasks; i++){
			int st = i * rowPerThread;
			int last;
			if(i == numtasks-1){
				last = rows;
			}else{
				last = st+rowPerThread;
			}
			MPI_Recv(&initials[(st)*columns], (last-st)*columns, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
		}
		for(int i = 0; i < (end-start)*columns; i++ ){
			initials[i] = cells[columns+i];
		}
	}else{
		MPI_Send(&cells[columns], (end-start)*columns, MPI_INT, 0, 0 , MPI_COMM_WORLD);
	}

	double endTime = MPI_Wtime();
	if(rank == MASTER){
		cout<<"TIME Taken "<<endTime-startTime<<endl;
		writeToFile(initials, rows, columns, outputFile);
	}
	MPI_Finalize();
	return 0;
}


int* allocateMemory(int x, int y){
	int* cells = new int[x*y];
	return cells;
}

void display(int* cells, int x, int y){
	for(int i = 0; i<x; i++){
		for(int j = 0; j<y; j++){
			cout<<cells[i*y+j]<<"\t";
		}
		cout<<endl;
	}
}

int* populate(int* mat, int x, int y){
	for(int i = 0; i<x; i++){
		for(int j = 0; j<y; j++){
			mat[i*y+j] = i+j;
		}
	}
	return mat;
}

int* modify(int* mat, int x, int y){
	for(int i = 0; i< x; i++){
		for(int j = 0; j<y; j++){
			mat[i*y+j] *= 2;
		}
	}
	return mat;
}

int findNumberOfAliveNeighbours(int rowIndex, int columnIndex, int rows, int columns, int* cells){
	int aliveCount = 0;
	if(rowIndex == 0 || rowIndex == rows-1)
		return aliveCount;
	
	if(rowIndex>0 && columnIndex >0 && rowIndex<rows-1 && columnIndex < columns-1){
		aliveCount = cells[(rowIndex)*columns+columnIndex-1]+cells[(rowIndex)*columns+columnIndex+1]+
					cells[(rowIndex+1)*columns+columnIndex-1]+cells[(rowIndex+1)*columns+columnIndex]+cells[(rowIndex+1)*columns+columnIndex+1]+
					cells[(rowIndex-1)*columns+columnIndex-1]+cells[(rowIndex-1)*columns+columnIndex]+cells[(rowIndex-1)*columns+columnIndex+1];
	}
	else if(columnIndex == 0){
		aliveCount = cells[(rowIndex-1)*columns] + cells[(rowIndex-1)*columns+1]+cells[((rowIndex-1)*columns) + (columns-1)]+
					cells[rowIndex*columns+1] + cells[((rowIndex)*columns) + (columns-1)]+
					cells[(rowIndex+1)*columns] + cells[(rowIndex+1)*columns+1]+cells[((rowIndex+1)*columns) + (columns-1)];

	}
	else if(columnIndex == columns - 1){
		aliveCount = cells[(rowIndex-1)*columns] + cells[(rowIndex-1)*columns+(columns-1)]+cells[((rowIndex-1)*columns)+columns-2]+
					cells[(rowIndex*columns)+columns-2] + cells[((rowIndex)*columns)]+
					cells[(rowIndex+1)*columns] + cells[(rowIndex+1)*columns+(columns-1)]+cells[((rowIndex+1)*columns)+columns-2];		
	}
	return aliveCount;
}

int findAliveOrDeadInNextGeneration(int rowIndex, int columnIndex, int rows, int columns, int* cells)
{
	int alive = 0;
	int aliveNeighbours = findNumberOfAliveNeighbours(rowIndex, columnIndex, rows, columns, cells);

	if(cells[rowIndex*columns+columnIndex]){
		if(aliveNeighbours < 2){
			alive = 0;
		}else if(aliveNeighbours == 2 || aliveNeighbours == 3){
			alive = 1;
		}else if(aliveNeighbours > 3){
			alive = 0;
		}
	}else{
		if(aliveNeighbours == 3){
			alive = 1;
		}
	}
	return alive;
}

void copyCells(int* source, int* destination, int sourceRows, int columns){
	for(int i = 0; i< sourceRows*columns; i++){
		destination[i] = source[i];
	}
}

void updateCells(int start, int end,int columns, int* cells, int upperNeighbor, int lowerNeighbor, int rank, MPI_Status& status, int numtasks, int threads){
	int rows = end-start+2;
	int* tempCells = new int[(rows)*columns];

	#pragma omp parallel num_threads(threads)
	{
		int numberOfThread = omp_get_num_threads();
		int rowPerThread = rows/numberOfThread;
		int remainder = rows%numberOfThread;

		int threadNumber = omp_get_thread_num();
				
		int ompStart, ompEnd;
		ompStart = threadNumber * rowPerThread;
		if(threadNumber == numberOfThread - 1){
			ompEnd = ompStart + rowPerThread + remainder;
		}else{
			ompEnd = ompStart+rowPerThread;
		}

		for(int i = ompStart; i < ompEnd; i++){
			for(int j = 0; j< columns; j++){
				tempCells[i*columns+j] = findAliveOrDeadInNextGeneration(i,j, rows, columns, cells );
			}
		}
	}
	
	if(rank != numtasks-1){
		if(rank != MASTER){
			MPI_Sendrecv(&tempCells[(end-start)*columns],columns,MPI_INT,lowerNeighbor,0,&tempCells[0], columns, MPI_INT, upperNeighbor, 0,MPI_COMM_WORLD, &status);
			MPI_Sendrecv(&tempCells[columns],columns,MPI_INT,upperNeighbor,0,&tempCells[(end-start+1)*columns], columns, MPI_INT, lowerNeighbor, 0,MPI_COMM_WORLD, &status);
		} else {
			MPI_Send(&tempCells[(end-start)*columns], columns, MPI_INT, lowerNeighbor, 0, MPI_COMM_WORLD);
			MPI_Recv(&tempCells[0], columns, MPI_INT, upperNeighbor, 0, MPI_COMM_WORLD, &status);
			MPI_Send(&tempCells[columns], columns, MPI_INT, upperNeighbor, 0, MPI_COMM_WORLD);
			MPI_Recv(&tempCells[(end-start+1)*columns], columns, MPI_INT, lowerNeighbor, 0, MPI_COMM_WORLD, &status);
		}		
	}else{
		MPI_Recv(&tempCells[0], columns, MPI_INT, upperNeighbor, 0, MPI_COMM_WORLD, &status);
		MPI_Send(&tempCells[(end-start)*columns], columns, MPI_INT, lowerNeighbor, 0, MPI_COMM_WORLD);
		MPI_Recv(&tempCells[(end-start+1)*columns], columns, MPI_INT, lowerNeighbor, 0, MPI_COMM_WORLD, &status);
		MPI_Send(&tempCells[columns], columns, MPI_INT, upperNeighbor, 0, MPI_COMM_WORLD);
	}

	copyCells(tempCells, cells, rows, columns);
	delete tempCells;
}

int* populate(int row, int column, ifstream& inFile){
	int* cells = new int[row*column];
	string values;
	int j = 0;
	getline(inFile, values);
	while(!inFile.eof()){
		for(int i = 0; i<values.length(); i++){
			if(values.at(i) != ' '){
				cells[j] = values.at(i)-'0';
				j++;
			}
		}
		getline(inFile, values);
	}
	return cells;
}
void print(int* mat,int x, int y){
	for(int i = 0; i<x; i++){
		for(int j = 0; j<y; j++){
			cout<<mat[i*y+j]<<" ";
		}
		cout<<endl;
	}
}

void writeToFile(int* mat, int row, int column, char* outputFile){
	ofstream outFile;
	outFile.open(outputFile, fstream::app);
	outFile<<row<<" "<<column<<endl;
	for(int i = 0; i<row; i++){
		for(int j =0; j< column; j++){
			outFile<<mat[i*column+j]<<" ";
		}
		outFile<<endl;
	}
	outFile<<endl;
}