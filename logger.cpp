#include <stdio.h>
#include <stdlib.h>

#pragma warning (disable : 4996)

#define MAX_CHANNELS 100
#define BASE_F 1000 // 1000Hz base frequency

void usage(void)
{
	fprintf(stderr, "Usage: \r\n"
	                "    encode.exe -f<File Name> -s<Start Time (ms)> -d<Duration (ms)> \r\n"
					"               -c<ChannelId>,<Frequency> [-c<ChannelId>,<Frequency> ...]\r\n"
	                "Up to %d channels can be defined.\r\n"
					"Example: \r\n"
	                "    encode.exe -fdata.dat -s456438 -d3 -c0,1000 -c1,200 -c2,500\r\n",
	        MAX_CHANNELS);
	exit(42);
}

unsigned Start = 0;
unsigned Duration = 0;
typedef struct TChannelDescription_ {
	int divider;
	int id;
} TChannelDescription;
TChannelDescription Channels[MAX_CHANNELS];
// dividers for all data acquisition frequencies
const unsigned FValues[] = {
	BASE_F/1000, BASE_F/500, BASE_F/200, BASE_F/100, BASE_F/50,
	BASE_F/20,   BASE_F/10,  BASE_F/5,   BASE_F/2,   BASE_F/1
};
const unsigned FValuesNum = sizeof(FValues) / sizeof(int);
int ChannelsNum = 0;
int BlockLen = 0;
FILE *DataFile = NULL;
char *DataFileName;

typedef struct TOutputData_ {
	int id;
	int frequency;
	int n; // mumber of elements in data array
	int *data;
} TOutputData;

TOutputData Outputs[MAX_CHANNELS];

int channel_compare(const void * p1, const void * p2)
{
	const TChannelDescription *pA = (TChannelDescription *)p1;
	const TChannelDescription *pB = (TChannelDescription *)p2;
	if (pA->divider < pB->divider)
		return -1;
	if (pA->divider > pB->divider)
		return 1;
	if (pA->id < pB->id)
		return -1;
	if (pA->id > pB->id)
		return 1;

	return 0; // todo: report an error
}

int get_value(unsigned channel, unsigned tick)
{
	return (channel << 24) + tick / Channels[channel].divider;
}

void do_encode(unsigned tick)
{
	int i;
	for (i = 0; i < ChannelsNum; i++) // channels are already sorted in frequency descending and ID ascending order
	{
		if (tick % Channels[i].divider == 0)
		{
			int value = get_value(Channels[i].id, tick); // get test value
			fwrite(&value, sizeof(value), 1, DataFile);
			BlockLen += sizeof(value);
		}
	}
}

int do_decode(unsigned tick, int len)
{
	int i;
	for (i = 0; i < ChannelsNum && len > 0; i++) // channels are already sorted in frequency descending and ID ascending order
	{
		if (tick % Channels[i].divider == 0)
		{
			int value;
			size_t read;
			read = fread(&value, sizeof(value), 1, DataFile);
			if (!read)
			{
				fprintf(stderr, "Cannot read data.\r\n");
				exit(12);
			}
			Outputs[i].data[Outputs[i].n] = value;
			Outputs[i].n++;
			len -= sizeof(value);
		}
	}
	return len;
}

void test_encode()
{
	unsigned int tick;
	unsigned int end = Start + Duration;

	DataFile = fopen(DataFileName, "wb");
	if (DataFile == NULL)
	{
		fprintf(stderr, "Cannot open output file %s\r\n", DataFileName);
		exit(4);
	}

	// start time
	fwrite(&Start, sizeof(Start), 1, DataFile);

	// dummy block length value
	fwrite(&BlockLen, sizeof(BlockLen), 1, DataFile);

	// encode channels
	for (tick = Start; tick < end; tick++)
	{
		do_encode(tick);
	}

	// update block length field
	fseek(DataFile, sizeof(Start), SEEK_SET);
	fwrite(&BlockLen, sizeof(BlockLen), 1, DataFile);

	fclose(DataFile);
}

void test_decode()
{
	unsigned int tick;
	int i;
	unsigned int start, duration, end;
	int block_len;
	int size1s;
	int read;

	DataFile = fopen(DataFileName, "rb");
	if (DataFile == NULL)
	{
		fprintf(stderr, "Cannot open output file %s\r\n", DataFileName);
		exit(4);
	}

	// start time
	read = fread(&start, sizeof(start), 1, DataFile);
	if (!read)
	{
		fprintf(stderr, "Cannot read start time.\r\n");
		exit(10);
	}

	// block length
	read = fread(&block_len, sizeof(block_len), 1, DataFile);
	if (!read)
	{
		fprintf(stderr, "Cannot read block length.\r\n");
		exit(11);
	}

	// get the size of 1s block
	size1s = 0;
	for (i = 0; i < ChannelsNum; i++)
	{
		size1s += (BASE_F / Channels[i].divider) * sizeof(int);
	}
	// get duration estimate (rounded up to 1000ms)
	duration = ((block_len + size1s - 1) / size1s) * BASE_F;

	// allocate memory for continuous blocks
	for (i = 0; i < ChannelsNum; i++)
	{
		size_t size;
		Outputs[i].n = 0;
		Outputs[i].id = Channels[i].id;
		Outputs[i].frequency = BASE_F / Channels[i].divider;
		size = (size_t)duration / Channels[i].divider * sizeof(int);
		Outputs[i].data = (int *)malloc(size);
	}

	// decode channels
	end = start + duration;
	for (tick = start; tick < end && block_len > 0; tick++)
	{
		block_len = do_decode(tick, block_len);
	}

	fclose(DataFile);

	// output results
	for (i = 0; i < ChannelsNum; i++)
	{
		int j;
		printf("Channel %d %dHz ", Outputs[i].id, Outputs[i].frequency);
		for (j = 0; j < Outputs[i].n; j++)
		{
			printf("%08x ", Outputs[i].data[j]);
		}
		printf("\r\n");
	}

	// deallocate memory
	for (i = 0; i < ChannelsNum; i++)
	{
		free(Outputs[i].data);
	}
}

int main(int argc, char* argv[])
{
	int i;
	if (argc < 2)
	{
		usage();
		exit(1);
	}

	for (i = 1; i < argc; i++)
	{
		char c = argv[i][1];
		if (argv[i][0] != '-')
		{
			usage();
			exit(2);
		}

		switch (c)
		{
		case 's':
			if (!sscanf(&argv[i][2], "%u", &Start))
			{
				usage();
				exit(3);
			}
			break;

		case 'd':
			if (!sscanf(&argv[i][2], "%u", &Duration))
			{
				usage();
				exit(3);
			}
			break;

		case 'c':
			{
				unsigned id;
				unsigned frequency;
				if (sscanf(&argv[i][2], "%u,%u", &id, &frequency) != 2 || frequency > BASE_F)
				{
					usage();
					exit(3);
				}
				// note: don't check for permissible values here, so for example 4 and 250 are possible
				Channels[ChannelsNum].divider = BASE_F / frequency;
				Channels[ChannelsNum].id = id;
				ChannelsNum++;
			}
			break;

		case 'f':
			{
				if (&argv[i][2])
				{
					DataFileName = &argv[i][2];
				}
				else
				{
					usage();
					exit(3);
				}
			}
			break;

		default:
			usage();
			exit(4);

		}

	}

	if (DataFileName == NULL)
	{
		fprintf(stderr, "Data file name not specified.\r\n");
		exit(5);
	}

	if (ChannelsNum == 0)
	{
		fprintf(stderr, "No channels specified.\r\n");
		exit(6);
	}

	if (Duration == 0)
	{
		fprintf(stderr, "Block duration not specified.\r\n");
		exit(7);
	}

	printf("Start at %ums, Duration %ums\r\n", Start, Duration);
	qsort(Channels, ChannelsNum, sizeof(Channels[0]), channel_compare);
	for (i = 0; i < ChannelsNum; i++)
	{
		printf("Channel %02d: %d Hz\r\n", Channels[i].id, BASE_F / Channels[i].divider);
	}

	test_encode();

	test_decode();

	return 0;
}

