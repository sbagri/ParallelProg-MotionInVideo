//#include "StdAfx.h"
#include <iostream>
#include <stdio.h>
#include <cuda_runtime_api.h>

#define MAX_FILE_NAME_CHARS 40
#define MAX_OUTFILE_NAME_CHARS 45
#define FRAMES_PER_ITER		256
#define PROMETHEUS_TESLA_C2075 1

using namespace std;

// Define this to turn on error checking
#define CUDA_ERROR_CHECK

#define CudaSafeCall( err ) __cudaSafeCall( err, __FILE__, __LINE__ )
#define CudaCheckError()    __cudaCheckError( __FILE__, __LINE__ )

inline void __cudaSafeCall( cudaError err, const char *file, const int line )
{
	#ifdef CUDA_ERROR_CHECK
    if ( cudaSuccess != err )
    {	fprintf( stderr, "cudaSafeCall() failed at %s:%i : %s\n",
                 file, line, cudaGetErrorString( err ) );
		exit( -1 );
    }
	#endif
    return;
}

inline void __cudaCheckError( const char *file, const int line )
{
	#ifdef CUDA_ERROR_CHECK
    cudaError err = cudaGetLastError();
    if ( cudaSuccess != err )
    {
        fprintf( stderr, "cudaCheckError() failed at %s:%i : %s\n",
                 file, line, cudaGetErrorString( err ) );
        exit( -1 );
    }

    // More careful checking. However, this will affect performance.
    // Comment away if needed.
    err = cudaDeviceSynchronize();
    if( cudaSuccess != err )
    {
        fprintf( stderr, "cudaCheckError() with sync failed at %s:%i : %s\n",
                 file, line, cudaGetErrorString( err ) );
        exit( -1 );
    }
	#endif
    return;
}

__global__ void cudafunc(char *in_file, char *out_file)
{
	unsigned long long StartIdx = ((blockIdx.x * blockDim.x) + threadIdx.x) * 128;
	unsigned long long EndIdx = ((blockIdx.x * blockDim.x) + threadIdx.x + 1) * 128;
	unsigned long long OneBlockSize = blockDim.x * 128;
	unsigned long long i;
	char temp;
	for(i=StartIdx; i<EndIdx; i++)
	{	
		temp = in_file[i + OneBlockSize] -  in_file[i];
		if(temp < 0)
			out_file[i] = (-1 * temp);
		else
			out_file[i] = temp;
	}
}

class Vid
{
public:
	FILE *InFile;
	FILE *OutFile;
	char FileName[MAX_FILE_NAME_CHARS];
	char OutFileName[MAX_OUTFILE_NAME_CHARS];
	int Wdth, Hght;
	int TotFrame;
	unsigned long long Totalbytes;
	float PlayTime;
	char *VideoFrameData;
	char *OutFrameData;
	int BytesPerFrame;
	int FramesInThisIter;

	bool init(char *);
	bool open_file();		//opens both input and output
	bool Prt_vid();
	bool ReadFrames();
};

bool Vid::init(char *Fname)
{
	if(MAX_FILE_NAME_CHARS < strlen(Fname))
	{
		printf("File Name is bigger than space allocated\n");
		exit(20);
	}
	
	memset(FileName, 0, MAX_FILE_NAME_CHARS);
	strcpy(FileName, Fname);
	memset(OutFileName, 0, MAX_OUTFILE_NAME_CHARS);
	memcpy(OutFileName, FileName, (strlen(FileName)-4));
	strcat(OutFileName, "_diff.yuv\0");

	Wdth = Hght = TotFrame =0;
	Totalbytes = TotFrame =0;
	return 1;
}

bool Vid::open_file()
{
	InFile = fopen(FileName, "rb");
	if(NULL == InFile)
	{
		printf("Possibly %s doesnt exist, please check, exiting", FileName);
		exit(21);
	}

	OutFile = fopen(OutFileName, "wb");
	if(NULL == OutFile)
	{	
		printf("Problem in creating output file, exiting", FileName);
		exit(22);
	}
	
	//gettting total number of frames
	if(!fseek(InFile, 0, SEEK_END))
	{	Totalbytes = ftell(InFile);
		fseek(InFile, 0, SEEK_SET);
		TotFrame = (int)(Totalbytes / ((Wdth * Hght * 3) / 2));
	}
	
	BytesPerFrame = (Wdth * Hght * 3)/2;
	return 1;
}

bool Vid::Prt_vid()
{
	cout<<" InFile : "<<InFile<<endl;
	cout<<" OutFile : "<<OutFile<<endl;
	cout<<" FileName : "<<FileName<<endl;
	cout<<" OutFileName : "<<OutFileName<<endl;
	cout<<" Wdth : "<<Wdth<<" Hght : "<<Hght<<endl;
	cout<<" Totalbytes : "<<Totalbytes<<endl;
	cout<<" TotFrame : "<<TotFrame<<endl;
	cout<<" BytesPerFrame : "<<BytesPerFrame<<endl;
	cout<<" Sizeof(char) : "<<sizeof(char)<<endl;
	return 1;
}


bool Vid::ReadFrames()
{
	return true;
}


int main(int argc, char* argv[])
{
//assume video is .y4m file with headers and "FRAME" to separate frame data
//of YUV 4:2:0 format
Vid Video;
if(4 != argc)
{	printf("More params needed, format is %s <Filename> <Width> <Height>", argv[0]);
}

Video.init(argv[1]);
Video.Wdth = (int) atoi(argv[2]);
Video.Hght = (int) atoi(argv[3]);
Video.open_file();
Video.Prt_vid();

int FramesPerIter = 256;

Video.VideoFrameData = new char [FramesPerIter * Video.BytesPerFrame];
Video.OutFrameData = new char[(FramesPerIter - 1) * Video.BytesPerFrame];

// select device #1, Tesla C2075
if( cudaSetDevice(PROMETHEUS_TESLA_C2075) != cudaSuccess ) exit( 1 );

char *c_InVid;
char *c_OutVid;
CudaSafeCall(cudaMalloc((void **)&c_InVid,(FramesPerIter * Video.BytesPerFrame * sizeof(char))));
CudaSafeCall(cudaMalloc((void **)&c_OutVid,((FramesPerIter - 1) * Video.BytesPerFrame * sizeof(char))));
memset(Video.OutFrameData, 100, ((FramesPerIter - 1) * Video.BytesPerFrame * sizeof(char)));

int FrameRemaining = Video.TotFrame;
while(FrameRemaining > 0)
{	
	int temp = FrameRemaining - FRAMES_PER_ITER;
	if(temp > 0)
	{
		Video.FramesInThisIter = FRAMES_PER_ITER;
		FrameRemaining = FrameRemaining - FRAMES_PER_ITER;
	}
	else
	{	//take the remaining no. of frames
		Video.FramesInThisIter = FrameRemaining;
		FrameRemaining = 0;
	}

	//read no. of frames
	cout<<" FramesInThisIter "<<Video.FramesInThisIter<<endl;
	if(ftell(Video.InFile) > (10 * Video.BytesPerFrame))//just to check not in beginning of file
	{	fseek(Video.InFile, -Video.BytesPerFrame, SEEK_CUR);
		FrameRemaining += 1;
	}
	fread(Video.VideoFrameData, sizeof(char), (Video.FramesInThisIter * Video.BytesPerFrame), Video.InFile);
	
	//send it to CUDA
	unsigned long long TtlBytesToSend = Video.FramesInThisIter * Video.BytesPerFrame * sizeof(char);
	cudaMemcpy(c_InVid, Video.VideoFrameData, TtlBytesToSend, cudaMemcpyHostToDevice);
	
	int blocks = Video.FramesInThisIter - 1;
	int threads = Video.BytesPerFrame / 128;
	cout<<" blocks "<<blocks<<endl;
	cout<<" threads "<<threads<<endl;
	cudafunc<<<blocks,threads>>>(c_InVid, c_OutVid);
	cout<<" After call done "<<endl;
	CudaCheckError();
	cout<<" CudaCheckeror done"<<endl;
	//CUDA does calculation
	cudaMemcpy(Video.OutFrameData, c_OutVid, ((Video.FramesInThisIter - 1)* Video.BytesPerFrame * sizeof(char)), cudaMemcpyDeviceToHost);
	//get it back from CUDA
	
	//concatenate it to ouput file
	fwrite(Video.OutFrameData, sizeof(char), ((Video.FramesInThisIter - 1)* Video.BytesPerFrame * sizeof(char)), Video.OutFile);
}

fflush(Video.OutFile);
fclose(Video.OutFile);
fclose(Video.InFile);
cudaFree(c_InVid);
cudaFree(c_OutVid);
free(Video.VideoFrameData);
free(Video.OutFrameData);

return 1;
}