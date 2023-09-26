// SPDX-License-Identifier: GPL-2.0
/*
 * Memory subsystem support
 *
 * Written by Matt Tolentino <matthew.e.tolentino@intel.com>
 *            Dave Hansen <haveblue@us.ibm.com>
 *
 * This file provides the necessary infrastructure to represent
 * a SPARSEMEM-memory-model system's physical memory in /sysfs.
 * All arch-independent code that assumes MEMORY_HOTPLUG requires
 * SPARSEMEM should be contained here, or in mm/memory_hotplug.c.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/topology.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/stat.h>
#include <linux/slab.h>

#include <linux/atomic.h>
#include <linux/uaccess.h>
#if defined(OPLUS_FEATURE_MULTI_FREEAREA) && defined(CONFIG_PHYSICAL_ANTI_FRAGMENTATION)
#include <linux/mmzone.h>
#endif
static DEFINE_MUTEX(mem_sysfs_mutex);

#define MEMORY_CLASS_NAME	"memory"

#define to_memory_block(dev) container_of(dev, struct memory_block, dev)

static int sections_per_block;

static inline int base_memory_block_id(int section_nr)
{
	return section_nr / sections_per_block;
}

static inline int pfn_to_block_id(unsigned long pfn)
{
	return base_memory_block_id(pfn_to_section_nr(pfn));
}

static int memory_subsys_online(struct device *dev);
static int memory_subsys_offline(struct device *dev);

static struct bus_type memory_subsys = {
	.name = MEMORY_CLASS_NAME,
	.dev_name = MEMORY_CLASS_NAME,
	.online = memory_subsys_online,
	.offline = memory_subsys_offline,
};

static BLOCKING_NOTIFIER_HEAD(memory_chain);

int register_memory_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&memory_chain, nb);
}
EXPORT_SYMBOL(register_memory_notifier);

void unregister_memory_notifier(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&memory_chain, nb);
}
EXPORT_SYMBOL(unregister_memory_notifier);

static ATOMIC_NOTIFIER_HEAD(memory_isolate_chain);

int register_memory_isolate_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&memory_isolate_chain, nb);
}
EXPORT_SYMBOL(register_memory_isolate_notifier);

void unregister_memory_isolate_notifier(struct notifier_block *nb)
{
	atomic_notifier_chain_unregister(&memory_isolate_chain, nb);
}
EXPORT_SYMBOL(unregister_memory_isolate_notifier);

static void memory_block_release(struct device *dev)
{
	struct memory_block *mem = to_memory_block(dev);

	kfree(mem);
}

unsigned long __weak memory_block_size_bytes(void)
{
	return MIN_MEMORY_BLOCK_SIZE;
}

static unsigned long get_memory_block_size(void)
{
	unsigned long block_sz;

	block_sz = memory_block_size_bytes();

	/* Validate blk_sz is a power of 2 and not less than section size */
	if ((block_sz & (block_sz - 1)) || (block_sz < MIN_MEMORY_BLOCK_SIZE)) {
		WARN_ON(1);
		block_sz = MIN_MEMORY_BLOCK_SIZE;
	}

	return block_sz;
}

/*
 * use this as the physical section index that this memsection
 * uses.
 */

static ssize_t phys_index_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct memory_block *mem = to_memory_block(dev);
	unsigned long phys_index;

	phys_index = mem->start_section_nr / sections_per_block;
	return sysfs_emit(buf, "%08lx\n", phys_index);
}

/*
 * Show whether the section of memory is likely to be hot-removable
 */
static ssize_t show_mem_removable(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned long i, pfn;
	int ret = 1;
	struct memory_block *mem = to_memory_block(dev);

	if (mem->state != MEM_ONLINE)
		goto out;

	for (i = 0; i < sections_per_block; i++) {
		if (!present_section_nr(mem->start_section_nr + i))
			continue;
		pfn = section_nr_to_pfn(mem->start_section_nr + i);
		ret &= is_mem_section_removable(pfn, PAGES_PER_SECTION);
	}

out:
	return sysfs_emit(buf, "%d\n", ret);
}

/*
 * online, offline, going offline, etc.
 */
static ssize_t show_mem_state(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct memory_block *mem = to_memory_block(dev);
	ssize_t len = 0;

	/*
	 * We can probably put these states in a nice little array
	 * so that they're not open-coded
	 */
	switch (mem->state) {
	case MEM_ONLINE:
		len = sysfs_emit(buf, "online\n");
		break;
	case MEM_OFFLINE:
		len = sysfs_emit(buf, "offline\n");
		break;
	case MEM_GOING_OFFLINE:
		len = sysfs_emit(buf, "going-offline\n");
		break;
	default:
		len = sysfs_emit(buf, "ERROR-UNKNOWN-%ld\n",
				 mem->state);
		WARN_ON(1);
		break;
	}

	return len;
}

int memory_notify(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&memory_chain, val, v);
}

int memory_isolate_notify(unsigned long val, void *v)
{
	return atomic_notifier_call_chain(&memory_isolate_chain, val, v);
}

/*
 * The probe routines leave the pages uninitialized, just as the bootmem code
 * does. Make sure we do not access them, but instead use only information from
 * within sections.
 */
static bool pages_correctly_probed(unsigned long start_pfn)
{
	unsigned long section_nr = pfn_to_section_nr(start_pfn);
	unsigned long section_nr_end = section_nr + sections_per_block;
	unsigned long pfn = start_pfn;

	/*
	 * memmap between sections is not contiguous except with
	 * SPARSEMEM_VMEMMAP. We lookup the page once per section
	 * and assume memmap is contiguous within each section
	 */
	for (; section_nr < section_nr_end; section_nr++) {
		if (WARN_ON_ONCE(!pfn_valid(pfn)))
			return false;

		if (!present_section_nr(section_nr)) {
			pr_warn("section %ld pfn[%lx, %lx) not present",
				section_nr, pfn, pfn + PAGES_PER_SECTION);
			return false;
		} else if (!valid_section_nr(section_nr)) {
			pr_warn("section %ld pfn[%lx, %lx) no valid memmap",
				section_nr, pfn, pfn + PAGES_PER_SECTION);
			return false;
		} else if (online_section_nr(section_nr)) {
			pr_warn("section %ld pfn[%lx, %lx) is already online",
				section_nr, pfn, pfn + PAGES_PER_SECTION);
			return false;
		}
		pfn += PAGES_PER_SECTION;
	}

	return true;
}

/*
 * MEMORY_HOTPLUG depends on SPARSEMEM in mm/Kconfig, so it is
 * OK to have direct references to sparsemem variables in here.
 */
static int
memory_block_action(unsigned long start_section_nr, unsigned long action,
		    int online_type)
{
	unsigned long start_pfn;
	unsigned long nr_pages = PAGES_PER_SECTION * sections_per_block;
	int ret;

	start_pfn = section_nr_to_pfn(start_section_nr);

	switch (action) {
	case MEM_ONLINE:
		if (!pages_correctly_probed(start_pfn))
			return -EBUSY;

		ret = online_pages(start_pfn, nr_pages, online_type);
		break;
	case MEM_OFFLINE:
		ret = offline_pages(start_pfn, nr_pages);
		break;
	default:
		WARN(1, KERN_WARNING "%s(%ld, %ld) unknown action: "
		     "%ld\n", __func__, start_section_nr, action, action);
		ret = -EINVAL;
	}

	return ret;
}

static int memory_block_change_state(struct memory_block *mem,
		unsigned long to_state, unsigned long from_state_req)
{
	int ret = 0;

	if (mem->state != from_state_req)
		return -EINVAL;

	if (to_state == MEM_OFFLINE)
		mem->state = MEM_GOING_OFFLINE;

	ret = memory_block_action(mem->start_section_nr, to_state,
				mem->online_type);

	mem->state = ret ? from_state_req : to_state;

	return ret;
}

/* The device lock serializes operations on memory_subsys_[online|offline] */
static int memory_subsys_online(struct device *dev)
{
	struct memory_block *mem = to_memory_block(dev);
	int ret;

	if (mem->state == MEM_ONLINE)
		return 0;

	/*
	 * If we are called from store_mem_state(), online_type will be
	 * set >= 0 Otherwise we were called from the device online
	 * attribute and need to set the online_type.
	 */
	if (mem->online_type < 0)
		mem->online_type = MMOP_ONLINE_KEEP;

	ret = memory_block_change_state(mem, MEM_ONLINE, MEM_OFFLINE);

	/* clear online_type */
	mem->online_type = -1;

	return ret;
}

static int memory_subsys_offline(struct device *dev)
{
	struct memory_block *mem = to_memory_block(dev);

	if (mem->state == MEM_OFFLINE)
		return 0;

	/* Can't offline block with non-present sections */
	if (mem->section_count != sections_per_block)
		return -EINVAL;

	return memory_block_change_state(mem, MEM_OFFLINE, MEM_ONLINE);
}

static ssize_t
store_mem_state(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct memory_block *mem = to_memory_block(dev);
	int ret, online_type;

	ret = lock_device_hotplug_sysfs();
	if (ret)
		return ret;

	if (sysfs_streq(buf, "online_kernel"))
		online_type = MMOP_ONLINE_KERNEL;
	else if (sysfs_streq(buf, "online_movable"))
		online_type = MMOP_ONLINE_MOVABLE;
	else if (sysfs_streq(buf, "online"))
		online_type = MMOP_ONLINE_KEEP;
	else if (sysfs_streq(buf, "offline"))
		online_type = MMOP_OFFLINE;
	else {
		ret = -EINVAL;
		goto err;
	}

	switch (online_type) {
	case MMOP_ONLINE_KERNEL:
	case MMOP_ONLINE_MOVABLE:
	case MMOP_ONLINE_KEEP:
		/* mem->online_type is protected by device_hotplug_lock */
		mem->online_type = online_type;
		ret = device_online(&mem->dev);
		break;
	case MMOP_OFFLINE:
		ret = device_offline(&mem->dev);
		break;
	default:
		ret = -EINVAL; /* should never happen */
	}

err:
	unlock_device_hotplug();

	if (ret < 0)
		return ret;
	if (ret)
		return -EINVAL;

	return count;
}

/*
 * phys_device is a bad name for this.  What I really want
 * is a way to differentiate between memory ranges that
 * are part of physical devices that constitute
 * a complete removable unit or fru.
 * i.e. do these ranges belong to the same physical device,
 * s.t. if I offline all of these sections I can then
 * remove the physical device?
 */
static ssize_t show_phys_device(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct memory_block *mem = to_memory_block(dev);
	return sysfs_emit(buf, "%d\n", mem->phys_device);
}

#ifdef CONFIG_MEMORY_HOTREMOVE
static void print_allowed_zone(char *buf, int nid, unsigned long start_pfn,
		unsigned long nr_pages, int online_type,
		struct zone *default_zone)
{
	struct zone *zone;

	zone = zone_for_pfn_range(online_type, nid, start_pfn, nr_pages);
	if (zone != default_zone) {
		strcat(buf, " ");
		strcat(buf, zone->name);
	}
}

static ssize_t show_valid_zones(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct memory_block *mem = to_memory_block(dev);
	unsigned long start_pfn = section_nr_to_pfn(mem->start_section_nr);
	unsigned long nr_pages = PAGES_PER_SECTION * sections_per_block;
	unsigned long valid_start_pfn, valid_end_pfn;
	struct zone *default_zone;
	int nid;

	/*
	 * Check the existing zone. Make sure that we do that only on the
	 * online nodes otherwise the page_zone is not reliable
	 */
	if (mem->state == MEM_ONLINE) {
		/*
		 * The block contains more than one zone can not be offlined.
		 * This can happen e.g. for ZONE_DMA and ZONE_DMA32
		 */
		if (!test_pages_in_a_zone(start_pfn, start_pfn + nr_pages,
					  &valid_start_pfn, &valid_end_pfn))
			return sysfs_emit(buf, "none\n");
		start_pfn = valid_start_pfn;
		strcat(buf, page_zone(pfn_to_page(start_pfn))->name);
		goto out;
	}

	nid = mem->nid;
	default_zone = zone_for_pfn_range(MMOP_ONLINE_KEEP, nid, start_pfn, nr_pages);
	strcat(buf, default_zone->name);

	print_allowed_zone(buf, nid, start_pfn, nr_pages, MMOP_ONLINE_KERNEL,
			default_zone);
	print_allowed_zone(buf, nid, start_pfn, nr_pages, MMOP_ONLINE_MOVABLE,
			default_zone);
out:
	strcat(buf, "\n");

	return strlen(buf);
}
static DEVICE_ATTR(valid_zones, 0444, show_valid_zones, NULL);
#endif

#ifdef CONFIG_MEMORY_HOTPLUG
static int count_num_free_block_pages(struct zone *zone, int bid)
{
#if defined(OPLUS_FEATURE_MULTI_FREEAREA) && defined(CONFIG_PHYSICAL_ANTI_FRAGMENTATION)
	int order, type, flc;
#else
	int order, type;
#endif
	unsigned long freecount = 0;
	unsigned long flags;

	spin_lock_irqsave(&zone->lock, flags);
	for (type = 0; type < MIGRATE_TYPES; type++) {
#if defined(OPLUS_FEATURE_MULTI_FREEAREA) && defined(CONFIG_PHYSICAL_ANTI_FRAGMENTATION)
		for (flc = 0; flc < FREE_AREA_COUNTS; flc++) {
			struct free_area *area;
                        struct page *page;
			for (order = 0; order < MAX_ORDER; ++order) {
                                area = &(zone->free_area[flc][order]);
				list_for_each_entry(page, &area->free_list[type], lru) {
                                        unsigned long pfn = page_to_pfn(page);
					int section_nr = pfn_to_section_nr(pfn);

                                if (bid == base_memory_block_id(section_nr))
                                        freecount += (1 << order);
				}
			}
		}
#else
		for (order = 0; order < MAX_ORDER; ++order) {
			struct free_area *area;
			struct page *page;

			area = &(zone->free_area[order]);
			list_for_each_entry(page, &area->free_list[type], lru) {
				unsigned long pfn = page_to_pfn(page);
				int section_nr = pfn_to_section_nr(pfn);

				if (bid == base_memory_block_id(section_nr))
					freecount += (1 << order);
			}

		}
#endif
	}
	spin_unlock_irqrestore(&zone->lock, flags);

	return freecount;
}

static ssize_t allocated_bytes_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct memory_block *mem = to_memory_block(dev);
	int block_id, free_pages;
	struct zone *movable_zone =
		&NODE_DATA(numa_node_id())->node_zones[ZONE_MOVABLE];
	unsigned long used, block_sz = get_memory_block_size();

	if (!populated_zone(movable_zone) || mem->state != MEM_ONLINE)
		return snprintf(buf, 100, "0\n");

	block_id = base_memory_block_id(mem->start_section_nr);
	free_pages = count_num_free_block_pages(movable_zone, block_id);
	used =  block_sz - (free_pages * PAGE_SIZE);

	return snprintf(buf, 100, "%lu\n", used);
}
#endif

static DEVICE_ATTR(phys_index, 0444, phys_index_show, NULL);
static DEVICE_ATTR(state, 0644, show_mem_state, store_mem_state);
static DEVICE_ATTR(phys_device, 0444, show_phys_device, NULL);
static DEVICE_ATTR(removable, 0444, show_mem_removable, NULL);
#ifdef CONFIG_MEMORY_HOTPLUG
static DEVICE_ATTR(allocated_bytes, 0444, allocated_bytes_show, NULL);
#endif

/*
 * Block size attribute stuff
 */
static ssize_t
print_block_size(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	return sysfs_emit(buf, "%lx\n", get_memory_block_size());
}

static DEVICE_ATTR(block_size_bytes, 0444, print_block_size, NULL);

/*
 * Memory auto online policy.
 */

static ssize_t
show_auto_online_blocks(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	if (memhp_auto_online)
		return sysfs_emit(buf, "online\n");
	else
		return sysfs_emit(buf, "offline\n");
}

static ssize_t
store_auto_online_blocks(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	if (sysfs_streq(buf, "online"))
		memhp_auto_online = true;
	else if (sysfs_streq(buf, "offline"))
		memhp_auto_online = false;
	else
		return -EINVAL;

	return count;
}

static DEVICE_ATTR(auto_online_blocks, 0644, show_auto_online_blocks,
		   store_auto_online_blocks);

/*
 * Some architectures will have custom drivers to do this, and
 * will not need to do it from userspace.  The fake hot-add code
 * as well as ppc64 will do all of their discovery in userspace
 * and will require this interface.
 */
#ifdef CONFIG_ARCH_MEMORY_PROBE
static ssize_t
memory_probe_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	u64 phys_addr;
	int nid, ret;
	unsigned long pages_per_block = PAGES_PER_SECTION * sections_per_block;

	ret = kstrtoull(buf, 0, &phys_addr);
	if (ret)
		return ret;

	if (phys_addr & ((pages_per_block << PAGE_SHIFT) - 1))
		return -EINVAL;

	ret = lock_device_hotplug_sysfs();
	if (ret)
		return ret;

	nid = memory_add_physaddr_to_nid(phys_addr);
	ret = __add_memory(nid, phys_addr,
			   MIN_MEMORY_BLOCK_SIZE * sections_per_block);

	if (ret)
		goto out;

	ret = count;
out:
	unlock_device_hotplug();
	return ret;
}

static DEVICE_ATTR(probe, S_IWUSR, NULL, memory_probe_store);

#ifdef CONFIG_MEMORY_HOTREMOVE
static ssize_t
memory_remove_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	u64 phys_addr;
	int nid, ret;
	unsigned long pages_per_block = PAGES_PER_SECTION * sections_per_block;

	ret = kstrtoull(buf, 0, &phys_addr);
	if (ret)
		return ret;

	if (phys_addr & ((pages_per_block << PAGE_SHIFT) - 1))
		return -EINVAL;

	nid = memory_add_physaddr_to_nid(phys_addr);
	ret = lock_device_hotplug_sysfs();
	if (ret)
		return ret;

	remove_memory(nid, phys_addr,
			 MIN_MEMORY_BLOCK_SIZE * sections_per_block);
	unlock_device_hotplug();
	return count;
}
static DEVICE_ATTR(remove, S_IWUSR, NULL, memory_remove_store);
#endif /* CONFIG_MEMORY_HOTREMOVE */
#endif /* CONFIG_ARCH_MEMORY_PROBE */

#ifdef CONFIG_MEMORY_FAILURE
/*
 * Support for offlining pages of memory
 */

/* Soft offline a page */
static ssize_t
store_soft_offline_page(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int ret;
	u64 pfn;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (kstrtoull(buf, 0, &pfn) < 0)
		return -EINVAL;
	pfn >>= PAGE_SHIFT;
	if (!pfn_valid(pfn))
		return -ENXIO;
	/* Only online pages can be soft-offlined (esp., not ZONE_DEVICE). */
	if (!pfn_to_online_page(pfn))
		return -EIO;
	ret = soft_offline_page(pfn_to_page(pfn), 0);
	return ret == 0 ? count : ret;
}

/* Forcibly offline a page, including killing processes. */
static ssize_t
store_hard_offline_page(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int ret;
	u64 pfn;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (kstrtoull(buf, 0, &pfn) < 0)
		return -EINVAL;
	pfn >>= PAGE_SHIFT;
	ret = memory_failure(pfn, 0);
	return ret ? ret : count;
}

static DEVICE_ATTR(soft_offline_page, S_IWUSR, NULL, store_soft_offline_page);
static DEVICE_ATTR(hard_offline_page, S_IWUSR, NULL, store_hard_offline_page);
#endif

/*
 * Note that phys_device is optional.  It is here to allow for
 * differentiation between which *physical* devices each
 * section belongs to...
 */
int __weak arch_get_memory_phys_device(unsigned long start_pfn)
{
	return 0;
}

/*
 * A reference for the returned object is held and the reference for the
 * hinted object is released.
 */
static struct memory_block *find_memory_block_by_id(int block_id,
						    struct memory_block *hint)
{
	struct device *hintdev = hint ? &hint->dev : NULL;
	struct device *dev;

	dev = subsys_find_device_by_id(&memory_subsys, block_id, hintdev);
	if (hint)
		put_device(&hint->dev);
	if (!dev)
		return NULL;
	return to_memory_block(dev);
}

struct memory_block *find_memory_block_hinted(struct mem_section *section,
					      struct memory_block *hint)
{
	int block_id = base_memory_block_id(__section_nr(section));

	return find_memory_block_by_id(block_id, hint);
}

/*
 * For now, we have a linear search to go find the appropriate
 * memory_block corresponding to a particular phys_index. If
 * this gets to be a real problem, we can always use a radix
 * tree or something here.
 *
 * This could be made generic for all device subsystems.
 */
struct memory_block *find_memory_block(struct mem_section *section)
{
	return find_memory_block_hinted(section, NULL);
}

static struct attribute *memory_memblk_attrs[] = {
	&dev_attr_phys_index.attr,
	&dev_attr_state.attr,
	&dev_attr_phys_device.attr,
	&dev_attr_removable.attr,
#ifdef CONFIG_MEMORY_HOTREMOVE
	&dev_attr_valid_zones.attr,
#endif
#ifdef CONFIG_MEMORY_HOTPLUG
	&dev_attr_allocated_bytes.attr,
#endif
	NULL
};

static struct attribute_group memory_memblk_attr_group = {
	.attrs = memory_memblk_attrs,
};

static const struct attribute_group *memory_memblk_attr_groups[] = {
	&memory_memblk_attr_group,
	NULL,
};

/*
 * register_memory - Setup a sysfs device for a memory block
 */
static
int register_memory(struct memory_block *memory)
{
	int ret;

	memory->dev.bus = &memory_subsys;
	memory->dev.id = memory->start_section_nr / sections_per_block;
	memory->dev.release = memory_block_release;
	memory->dev.groups = memory_memblk_attr_groups;
	memory->dev.offline = memory->state == MEM_OFFLINE;

	ret = device_register(&memory->dev);
	if (ret)
		put_device(&memory->dev);

	return ret;
}

static int init_memory_block(struct memory_block **memory, int block_id,
			     unsigned long state)
{
	struct memory_block *mem;
	unsigned long start_pfn;
	int ret = 0;

	mem = find_memory_block_by_id(block_id, NULL);
	if (mem) {
		put_device(&mem->dev);
		return -EEXIST;
	}
	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	mem->start_section_nr = block_id * sections_per_block;
	mem->end_section_nr = mem->start_section_nr + sections_per_block - 1;
	mem->state = state;
	start_pfn = section_nr_to_pfn(mem->start_section_nr);
	mem->phys_device = arch_get_memory_phys_device(start_pfn);
	mem->nid = NUMA_NO_NODE;

	ret = register_memory(mem);

	*memory = mem;
	return ret;
}

static int add_memory_block(int base_section_nr)
{
	struct memory_block *mem;
	int i, ret, section_count = 0;

	for (i = base_section_nr;
	     i < base_section_nr + sections_per_block;
	     i++)
		if (present_section_nr(i))
			section_count++;

	if (section_count == 0)
		return 0;
	ret = init_memory_block(&mem, base_memory_block_id(base_section_nr),
				MEM_ONLINE);
	if (ret)
		return ret;
	mem->section_count = section_count;
	return 0;
}

static void unregister_memory(struct memory_block *memory)
{
	if (WARN_ON_ONCE(memory->dev.bus != &memory_subsys))
		return;

	/* drop the ref. we got via find_memory_block() */
	put_device(&memory->dev);
	device_unregister(&memory->dev);
}

/*
 * Create memory block devices for the given memory area. Start and size
 * have to be aligned to memory block granularity. Memory block devices
 * will be initialized as offline.
 */
int create_memory_block_devices(unsigned long start, unsigned long size)
{
	const int start_block_id = pfn_to_block_id(PFN_DOWN(start));
	int end_block_id = pfn_to_block_id(PFN_DOWN(start + size));
	struct memory_block *mem;
	unsigned long block_id;
	int ret = 0;

	if (WARN_ON_ONCE(!IS_ALIGNED(start, memory_block_size_bytes()) ||
			 !IS_ALIGNED(size, memory_block_size_bytes())))
		return -EINVAL;

	mutex_lock(&mem_sysfs_mutex);
	for (block_id = start_block_id; block_id != end_block_id; block_id++) {
		ret = init_memory_block(&mem, block_id, MEM_OFFLINE);
		if (ret)
			break;
		mem->section_count = sections_per_block;
	}
	if (ret) {
		end_block_id = block_id;
		for (block_id = start_block_id; block_id != end_block_id;
		     block_id++) {
			mem = find_memory_block_by_id(block_id, NULL);
			if (WARN_ON_ONCE(!mem))
				continue;
			mem->section_count = 0;
			unregister_memory(mem);
		}
	}
	mutex_unlock(&mem_sysfs_mutex);
	return ret;
}

/*
 * Remove memory block devices for the given memory area. Start and size
 * have to be aligned to memory block granularity. Memory block devices
 * have to be offline.
 */
void remove_memory_block_devices(unsigned long start, unsigned long size)
{
	const int start_block_id = pfn_to_block_id(PFN_DOWN(start));
	const int end_block_id = pfn_to_block_id(PFN_DOWN(start + size));
	struct memory_block *mem;
	int block_id;

	if (WARN_ON_ONCE(!IS_ALIGNED(start, memory_block_size_bytes()) ||
			 !IS_ALIGNED(size, memory_block_size_bytes())))
		return;

	mutex_lock(&mem_sysfs_mutex);
	for (block_id = start_block_id; block_id != end_block_id; block_id++) {
		mem = find_memory_block_by_id(block_id, NULL);
		if (WARN_ON_ONCE(!mem))
			continue;
		mem->section_count = 0;
		unregister_memory_block_under_nodes(mem);
		unregister_memory(mem);
	}
	mutex_unlock(&mem_sysfs_mutex);
}

/* return true if the memory block is offlined, otherwise, return false */
bool is_memblock_offlined(struct memory_block *mem)
{
	return mem->state == MEM_OFFLINE;
}

static struct attribute *memory_root_attrs[] = {
#ifdef CONFIG_ARCH_MEMORY_PROBE
	&dev_attr_probe.attr,
#ifdef CONFIG_MEMORY_HOTREMOVE
	&dev_attr_remove.attr,
#endif
#endif

#ifdef CONFIG_MEMORY_FAILURE
	&dev_attr_soft_offline_page.attr,
	&dev_attr_hard_offline_page.attr,
#endif

	&dev_attr_block_size_bytes.attr,
	&dev_attr_auto_online_blocks.attr,
	NULL
};

static struct attribute_group memory_root_attr_group = {
	.attrs = memory_root_attrs,
};

static const struct attribute_group *memory_root_attr_groups[] = {
	&memory_root_attr_group,
	NULL,
};

/*
 * Initialize the sysfs support for memory devices...
 */
int __init memory_dev_init(void)
{
	unsigned int i;
	int ret;
	int err;
	unsigned long block_sz;

	ret = subsys_system_register(&memory_subsys, memory_root_attr_groups);
	if (ret)
		goto out;

	block_sz = get_memory_block_size();
	sections_per_block = block_sz / MIN_MEMORY_BLOCK_SIZE;

	/*
	 * Create entries for memory sections that were found
	 * during boot and have been initialized
	 */
	mutex_lock(&mem_sysfs_mutex);
	for (i = 0; i <= __highest_present_section_nr;
		i += sections_per_block) {
		err = add_memory_block(i);
		if (!ret)
			ret = err;
	}
	mutex_unlock(&mem_sysfs_mutex);

out:
	if (ret)
		printk(KERN_ERR "%s() failed: %d\n", __func__, ret);
	return ret;
}

struct for_each_memory_block_cb_data {
	walk_memory_blocks_func_t func;
	void *arg;
};

static int for_each_memory_block_cb(struct device *dev, void *data)
{
	struct memory_block *mem = to_memory_block(dev);
	struct for_each_memory_block_cb_data *cb_data = data;

	return cb_data->func(mem, cb_data->arg);
}

/**
 * for_each_memory_block - walk through all present memory blocks
 *
 * @arg: argument passed to func
 * @func: callback for each memory block walked
 *
 * This function walks through all present memory blocks, calling func on
 * each memory block.
 *
 * In case func() returns an error, walking is aborted and the error is
 * returned.
 */
int for_each_memory_block(void *arg, walk_memory_blocks_func_t func)
{
	struct for_each_memory_block_cb_data cb_data = {
		.func = func,
		.arg = arg,
	};

	return bus_for_each_dev(&memory_subsys, NULL, &cb_data,
				for_each_memory_block_cb);
}
