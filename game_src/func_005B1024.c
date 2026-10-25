/* Native stand-in (not matched): 005B1024 is a lone `jr $ra` between the data-cache routine
 * 005B0F78 and 005B1028, with no caller and no reference in the image (its delay slot would be
 * 005B1028's first instruction). It does nothing; natively it just returns. */
void func_005B1024(void)
{
}
