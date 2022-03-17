int GenZero(int n)
{
    return 0;
}

int GenMax(int n)
{
    return n-1;
}

unsigned char ReverseBitsInByte(unsigned char input)
{
    // For details about this bit-reversal algorithm, see https://graphics.stanford.edu/~seander/bithacks.html#ReverseByteWith64BitsDiv
    // or http://www.inwap.com/pdp10/hbaker/hakmem/hacks.html (item 167)
    return (input * 0x0202020202ULL & 0x010884422010ULL) % 1023;
}
