#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include "functions.h"
#include "help.h"

int main( int argc, char **argv )
{
	int	start=0,	// start position for reporting (specify 0 for whole-chromosome)
		end=250000000,	// end position for reporting (specify chr length for whole-chromosome)
		dist = 200,	// upstream/downstream parameter for searching each locus for bad read pairs
		minq = 20,	// minimum mapping quality to consider a good read
		maxfrag = 2000,	// maximum fragment length permitted (distance between read pairs)
		field,		// field counter to identify right columns from bam
		flag,		// bitwise flag
		pos,		// read position
		mapq,		// mapping quality of read
		pnext,		// pos of next segment
		tlen,		// template length
		nm,		// NM field in bam file (edit distance of read to reference)
		startpos,	// start position of read's influence
		endpos,		// end position of read's influence
		direction,	// direction of read's influence
		seqlen,		// length of sequence
		hashval,	// holder for current value of good/badmapped hash when looking up or incrementing
		hashval2,	// same as hashval
		i,		// counter
		option_index=0,	// option counter for getopt
		c;		// character initializer for getopt

	char	line[BUFFSIZE],	// lines of SAM-format input
		*refgenome,// path to reference genome
		*refseq,	// reference sequence for read
		*rnext,		// RNEXT string (reference name for next segment)
		*token;		// for string spliting
		
	float	editdist = 1.0,	// min edit distance (as % of length) to consider read as bad
		result;		// placeholder for results while printing
	
	const char *delim ="\t";// tab delimiter for line inputs
	
	fai	refinfo;	// fai structure holding index information for reference genome

	//Get user-defined parameters
	static struct option long_options[] =
	{
		{"add", 1, 0, 0},
		{"start",required_argument,0,'s'},		// start position for reporting rescan statistics
		{"end",required_argument,0,'e'},		// end position for reporting
		{"distance",required_argument,0,'d'},		// up/downstream distance for searching
		{"levenshtein",required_argument,0,'l'},	// edit distance above which to consider a read bad
		{"minq",required_argument,0,'q'},		// minimum mapping quality for good reads
		{"refgenome",required_argument,0,'r'},		// reference genome
		{"help",no_argument,0,'h'},			// ask for help
		{NULL, 0, NULL, 0}
	};

	refgenome = "/home/russell/Dropbox/Data_and_resources/Genomes/hg19.fa.fai";
	refinfo = load_fai( *refgenome );
	
	while ((c = getopt_long(argc, argv, "hs:e:d:l:q:r:", long_options, &option_index)) != -1)
	{
		int this_option_optind = optind ? optind : 1;
		switch (c)
		{
			case 'h': usage(); return 0;
			case 's': start = atoi(optarg); break;
			case 'e': end = atoi(optarg); break;
			case 'd': dist = atoi(optarg); break;
			case 'l': sscanf( optarg, "%f", &editdist ); break;
			case 'q': minq = atoi(optarg); break;
			case 'r': refgenome = optarg; break;
		}
	}
	hash 	*goodmapped = hash_new( 1000000000 ),
		*badmapped  = hash_new( 1000000000 );

	while ( fgets ( line, sizeof line, stdin ) != NULL )
	{
		// initialize useful values
		field = 0;		// for extracting SAM fields
		nm = 0;			// clear previous nm value
		char *linep = line;	// pointer for looping through line elements
		
		while(token = strsep(&linep, delim))
		{
			switch(field)
			{
				case 1 : flag = atoi(token); break;	// flag now contains bitwise SAM flag
				case 3 : pos = atoi(token); break;	// pos now contains mapping position of read
				case 4 : mapq = atoi(token); break;	// mapq now contains mapping quality of read
				case 6 : rnext = token; break;		// rnext now contains chr of next read in pair
				case 7 : pnext = atoi(token); break;	// pnext now contains pos of next read in pair
				case 8 : tlen = atoi(token); break;	// tlen now contains template length
				case 9 : seqlen = strlen(token); break;	// seqlen now contains length of read
			}
			if( strstr( token, "NM:" ) != NULL )		// extract NM field, if available
			{
				nm = atoi(token+5);
			}
			field++;
		}
				
		// if mapping quality < user's minimum, skip to next read
		if( mapq < minq ) { continue; }

		//TODO: correct these so that they don't count non-spanning fragments
		if( ( flag & 16 ) == 16 ) // segment is reverse complemented
		{
			startpos = pos - dist;
			endpos = pos + seqlen;
			direction = -1;
		}
		else // segment is NOT reverse complemented
		{
			startpos = pos;
			endpos = pos + seqlen + dist;
			direction = 1;
		}
		
		// skip if fragment doesn't overlap region of interest
		if( endpos < start | startpos > end ) {	continue; }
		
		if( ( flag & 4 ) == 0 ) // segment is mapped adequately
		{
			if( ( flag & 8 ) == 0 ) // next segment is mapped adequately
			{
				if( !strcmp(rnext,"=") ) // next segment is on same chromosome
				{
					if( ( pnext - pos ) * direction < maxfrag ) // next segment is within 2kb
					{
						//TODO: populate hash of read IDs to ignore (avoid second counting later)
						if( ( (float)nm/seqlen ) < editdist )
						{
						//	fprintf(stdout,"aah%f %d %f\n",((float)nm/seqlen),nm,editdist);
							increment( goodmapped, startpos, endpos );
						}
						else
						{
						//	fprintf(stdout,"aah\n");
							increment( badmapped, pos, (pos+seqlen) );
						}
					}
					else // next segment is on same chr but too far away (probably rare)
					{
						increment( badmapped, startpos, endpos );
					}
				}
				else // next segment is on a different chromosome
				{
					increment( badmapped, startpos, endpos );
				}
			}
			else // next segment is not mapped adequately
			{
				increment( badmapped, startpos, endpos );
			}
		}
	}
	for( i = start; i < end; i++ )
	{
		hashval = hash_lookup( badmapped, i );
		hashval2= hash_lookup( goodmapped,i );
		if( hashval == NULL ) { hashval = 0; }
		if( hashval2== NULL ) { hashval2= 0; }
		if( hashval > 0 )
		{
			result = (float)hashval/((float)hashval+(float)hashval2);
			fprintf( stdout, "%f\t", result );
		}
		else
		{
			fprintf( stdout, "0\t" );
		}
	}
	fprintf( stdout, "\n" );
	return 0;
}
