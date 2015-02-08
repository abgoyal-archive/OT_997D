
#if !defined(__CA9_SLT__)
#define __CA9_SLT__

#define SLT_LOOP_CNT    (10000)
#define SLT_VA (0x70000000)
//#define __SLT_USE_GFP__

extern int slt_get_section(const unsigned int iCP);
extern void slt_free_section(void);

#endif  /*  __CA9_SLT__ */
