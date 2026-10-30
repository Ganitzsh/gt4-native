/* compiler: ee-gcc2.9-as296 */
/* Native version of 005AE630 _sceIDC (cache.o): invalidated the data cache lines (both ways) whose tag lay in
   [start, end], by index with `cache` instructions. Native memory is coherent and no DMA engine writes game RAM
   behind the CPU's back, so there is nothing to do. */
void func_005AE630(unsigned int start, unsigned int end)
{
    (void)start;
    (void)end;
}
