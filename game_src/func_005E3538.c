/* Native version of 005E3538 (from its matched source): its PLZCW in plain C. */

/* PLZCW of n-1 (its leading bits equal to the sign bit, less one) & 31, ^ 31: the bit length of n-1 (of ~(n-1)
   when negative); n is divided by 2 to that power, the power of two at or above n. */
float func_005E3538(int unused, unsigned int n)
{
    unsigned int x = n - 1;
    int shift = 0;
    if ((int)x < 0) x = ~x;
    while (x) {
        shift++;
        x >>= 1;
    }
    return (float)n / (float)(unsigned int)(1u << shift);
}
