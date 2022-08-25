#include <linux/version.h>
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
#include <../drivers/staging/android/ion/ion.h>
#else
#include <../drivers/staging/android/ion/heaps/msm_ion_priv.h>
#endif
#else
#include <../drivers/staging/android/aosp_ion/ion.h>
#endif

atomic_long_t ion_total_size;
bool ion_cnt_enable = true;

unsigned long ion_total(void)
{
	if (!ion_cnt_enable)
		return 0;
	return (unsigned long)atomic_long_read(&ion_total_size);
}
