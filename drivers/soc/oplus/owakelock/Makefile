#ifdef OPLUS_FEATURE_POWERINFO_STANDBY
#SunFaliang@BSP.Power.Basic, 2020/05/01, add for wakelock profiler

#code deprecated ... start (Todo : delete this code section)
ifeq ($(CONFIG_OPPO_POWER_MTK),y)
	obj-$(CONFIG_OPPO_WAKELOCK_PROFILER)	+= oplus_wakelock_profiler_mtk.o
else ifeq ($(CONFIG_OPPO_POWER_QCOM),y)
	obj-$(CONFIG_OPPO_WAKELOCK_PROFILER)    += oplus_wakelock_profiler_qcom.o
else ifeq ($(CONFIG_OPPO_WAKELOCK_PROFILER),y)
	obj-y                                   += oplus_wakelock_profiler.o
endif
#code deprecated ... end (Todo : delete this code section)

ifeq ($(CONFIG_OPLUS_POWER_MTK),y)
	obj-$(CONFIG_OPLUS_WAKELOCK_PROFILER)	+= oplus_wakelock_profiler_mtk.o
else ifeq ($(CONFIG_OPLUS_POWER_QCOM),y)
	obj-$(CONFIG_OPLUS_WAKELOCK_PROFILER)    += oplus_wakelock_profiler_qcom.o
else ifeq ($(CONFIG_OPLUS_WAKELOCK_PROFILER),y)
	obj-y                                   += oplus_wakelock_profiler.o
endif

#endif /* OPLUS_FEATURE_POWERINFO_STANDBY */
