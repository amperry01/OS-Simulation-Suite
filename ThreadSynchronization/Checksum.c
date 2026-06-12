//===================================================== file = checksum.c =====
//=  Program to compute 16-bit Internet checksum                              =
//=============================================================================
//=  Notes: 1) Based on the C-code given in RFC 1071 (Computing the Internet  =
//=            Checksum by R. Braden, D. Borman, and C. Partridge, 1988).     =
//=         2) Streamlined for 32-bit machines.  All overflows are added-in   =
//=            after the summing loop and not within the summing loop on a    =
//=            an overflow-by-overflow basis.                                 =
//=---------------------------------------------------------------------------=
//=  Build:  bcc32 checksum.c, gcc checksum.c                                 =
//=---------------------------------------------------------------------------=
//=  History:  KJC (8/25/00) - Genesis                                        =
//=============================================================================
//----- Include files ---------------------------------------------------------
#include <stdio.h>                  // Needed for printf()
#include <stdint.h>                 // Needed for int sizes
#include <stdlib.h>		    // Needed for rand()

//----- Defines ---------------------------------------------------------------
#define BUFFER_LEN        4096      // Length of buffer

//=============================================================================
//=  Compute Internet Checksum for count bytes beginning at location addr     =
//=============================================================================
uint16_t checksum(char *addr, uint32_t count)
{
    register uint32_t sum = 0;

    uint16_t *buf = (uint16_t *) addr;

    // Main summing loop
    while(count > 1)
    {
        sum = sum + *(buf)++;
        count = count - 2;
    }

    // Add left-over byte, if any
    if (count > 0)
        sum = sum + *addr;

    // Fold 32-bit sum to 16 bits
    while (sum>>16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return(~sum);

}

//====================================================================
// = Test code to demostrate how the checksum works
//====================================================================
int main(int argc, char *argv[])
{

    uint8_t   buff[BUFFER_LEN];
    uint16_t  cksum = 0;
    int       nbytes;

    if (argc != 2) {
        printf("Error:: Usage: %s <nbytes> \n", argv[0]);
	return -1;
    }
 
    nbytes = atoi(argv[1]);
    if ((nbytes <= 0) || (nbytes > BUFFER_LEN)) {
        printf("Invalid number of bytes: %d \n", nbytes);
        return -1;
    }

    // Load buffer with nbytes random bytes
    for (int i=0; i<nbytes; i++)  {
        buff[i] = (uint8_t)(rand() % 256);
    }

    // Compute the 16-bit checksum
    cksum = checksum((char *)buff, nbytes);
    printf("Checksum = %04X \n", cksum);

    return 0;
}

