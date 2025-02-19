/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABSP_H_
#define HABANALABSP_H_

#include "../include/common/armcp_if.h"
#include "../include/common/qman_if.h"
#include <uapi/misc/habanalabs.h>

#include <linux/cdev.h>
#include <linux/iopoll.h>
#include <linux/irqreturn.h>
#include <linux/dma-fence.h>
#include <linux/dma-direction.h>
#include <linux/scatterlist.h>
#include <linux/hashtable.h>

#define HL_NAME				"habanalabs"

#define HL_MMAP_CB_MASK			(0x8000000000000000ull >> PAGE_SHIFT)

#define HL_PENDING_RESET_PER_SEC	30

#define HL_HARD_RESET_MAX_TIMEOUT	120

#define HL_DEVICE_TIMEOUT_USEC		1000000 /* 1 s */

#define HL_HEARTBEAT_PER_USEC		5000000 /* 5 s */

#define HL_PLL_LOW_JOB_FREQ_USEC	5000000 /* 5 s */

#define HL_ARMCP_INFO_TIMEOUT_USEC	10000000 /* 10s */
#define HL_ARMCP_EEPROM_TIMEOUT_USEC	10000000 /* 10s */

#define HL_PCI_ELBI_TIMEOUT_MSEC	10 /* 10ms */

#define HL_SIM_MAX_TIMEOUT_US		10000000 /* 10s */

#define HL_IDLE_BUSY_TS_ARR_SIZE	4096

/* Memory */
#define MEM_HASH_TABLE_BITS		7 /* 1 << 7 buckets */

/* MMU */
#define MMU_HASH_TABLE_BITS		7 /* 1 << 7 buckets */

/*
 * HL_RSVD_SOBS 'sync stream' reserved sync objects per QMAN stream
 * HL_RSVD_MONS 'sync stream' reserved monitors per QMAN stream
 */
#define HL_RSVD_SOBS			4
#define HL_RSVD_MONS			2

#define HL_RSVD_SOBS_IN_USE		2
#define HL_RSVD_MONS_IN_USE		1

#define HL_MAX_SOB_VAL			(1 << 15)

#define IS_POWER_OF_2(n)		(n != 0 && ((n & (n - 1)) == 0))
#define IS_MAX_PENDING_CS_VALID(n)	(IS_POWER_OF_2(n) && (n > 1))

#define HL_PCI_NUM_BARS			6

/**
 * struct pgt_info - MMU hop page info.
 * @node: hash linked-list node for the pgts shadow hash of pgts.
 * @phys_addr: physical address of the pgt.
 * @shadow_addr: shadow hop in the host.
 * @ctx: pointer to the owner ctx.
 * @num_of_ptes: indicates how many ptes are used in the pgt.
 *
 * The MMU page tables hierarchy is placed on the DRAM. When a new level (hop)
 * is needed during mapping, a new page is allocated and this structure holds
 * its essential information. During unmapping, if no valid PTEs remained in the
 * page, it is freed with its pgt_info structure.
 */
struct pgt_info {
	struct hlist_node	node;
	u64			phys_addr;
	u64			shadow_addr;
	struct hl_ctx		*ctx;
	int			num_of_ptes;
};

struct hl_device;
struct hl_fpriv;

/**
 * enum hl_pci_match_mode - pci match mode per region
 * @PCI_ADDRESS_MATCH_MODE: address match mode
 * @PCI_BAR_MATCH_MODE: bar match mode
 */
enum hl_pci_match_mode {
	PCI_ADDRESS_MATCH_MODE,
	PCI_BAR_MATCH_MODE
};

/**
 * enum hl_fw_component - F/W components to read version through registers.
 * @FW_COMP_UBOOT: u-boot.
 * @FW_COMP_PREBOOT: preboot.
 */
enum hl_fw_component {
	FW_COMP_UBOOT,
	FW_COMP_PREBOOT
};

/**
 * enum hl_queue_type - Supported QUEUE types.
 * @QUEUE_TYPE_NA: queue is not available.
 * @QUEUE_TYPE_EXT: external queue which is a DMA channel that may access the
 *                  host.
 * @QUEUE_TYPE_INT: internal queue that performs DMA inside the device's
 *			memories and/or operates the compute engines.
 * @QUEUE_TYPE_CPU: S/W queue for communication with the device's CPU.
 * @QUEUE_TYPE_HW: queue of DMA and compute engines jobs, for which completion
 *                 notifications are sent by H/W.
 */
enum hl_queue_type {
	QUEUE_TYPE_NA,
	QUEUE_TYPE_EXT,
	QUEUE_TYPE_INT,
	QUEUE_TYPE_CPU,
	QUEUE_TYPE_HW
};

enum hl_cs_type {
	CS_TYPE_DEFAULT,
	CS_TYPE_SIGNAL,
	CS_TYPE_WAIT
};

/*
 * struct hl_inbound_pci_region - inbound region descriptor
 * @mode: pci match mode for this region
 * @addr: region target address
 * @size: region size in bytes
 * @offset_in_bar: offset within bar (address match mode)
 * @bar: bar id
 */
struct hl_inbound_pci_region {
	enum hl_pci_match_mode	mode;
	u64			addr;
	u64			size;
	u64			offset_in_bar;
	u8			bar;
};

/*
 * struct hl_outbound_pci_region - outbound region descriptor
 * @addr: region target address
 * @size: region size in bytes
 */
struct hl_outbound_pci_region {
	u64	addr;
	u64	size;
};

/*
 * struct hl_hw_sob - H/W SOB info.
 * @hdev: habanalabs device structure.
 * @kref: refcount of this SOB. The SOB will reset once the refcount is zero.
 * @sob_id: id of this SOB.
 * @q_idx: the H/W queue that uses this SOB.
 */
struct hl_hw_sob {
	struct hl_device	*hdev;
	struct kref		kref;
	u32			sob_id;
	u32			q_idx;
};

/**
 * struct hw_queue_properties - queue information.
 * @type: queue type.
 * @driver_only: true if only the driver is allowed to send a job to this queue,
 *               false otherwise.
 * @requires_kernel_cb: true if a CB handle must be provided for jobs on this
 *                      queue, false otherwise (a CB address must be provided).
 * @supports_sync_stream: True if queue supports sync stream
 */
struct hw_queue_properties {
	enum hl_queue_type	type;
	u8			driver_only;
	u8			requires_kernel_cb;
	u8			supports_sync_stream;
};

/**
 * enum vm_type_t - virtual memory mapping request information.
 * @VM_TYPE_USERPTR: mapping of user memory to device virtual address.
 * @VM_TYPE_PHYS_PACK: mapping of DRAM memory to device virtual address.
 */
enum vm_type_t {
	VM_TYPE_USERPTR = 0x1,
	VM_TYPE_PHYS_PACK = 0x2
};

/**
 * enum hl_device_hw_state - H/W device state. use this to understand whether
 *                           to do reset before hw_init or not
 * @HL_DEVICE_HW_STATE_CLEAN: H/W state is clean. i.e. after hard reset
 * @HL_DEVICE_HW_STATE_DIRTY: H/W state is dirty. i.e. we started to execute
 *                            hw_init
 */
enum hl_device_hw_state {
	HL_DEVICE_HW_STATE_CLEAN = 0,
	HL_DEVICE_HW_STATE_DIRTY
};

/**
 * struct hl_mmu_properties - ASIC specific MMU address translation properties.
 * @start_addr: virtual start address of the memory region.
 * @end_addr: virtual end address of the memory region.
 * @hop0_shift: shift of hop 0 mask.
 * @hop1_shift: shift of hop 1 mask.
 * @hop2_shift: shift of hop 2 mask.
 * @hop3_shift: shift of hop 3 mask.
 * @hop4_shift: shift of hop 4 mask.
 * @hop0_mask: mask to get the PTE address in hop 0.
 * @hop1_mask: mask to get the PTE address in hop 1.
 * @hop2_mask: mask to get the PTE address in hop 2.
 * @hop3_mask: mask to get the PTE address in hop 3.
 * @hop4_mask: mask to get the PTE address in hop 4.
 * @page_size: default page size used to allocate memory.
 */
struct hl_mmu_properties {
	u64	start_addr;
	u64	end_addr;
	u64	hop0_shift;
	u64	hop1_shift;
	u64	hop2_shift;
	u64	hop3_shift;
	u64	hop4_shift;
	u64	hop0_mask;
	u64	hop1_mask;
	u64	hop2_mask;
	u64	hop3_mask;
	u64	hop4_mask;
	u32	page_size;
};

/**
 * struct asic_fixed_properties - ASIC specific immutable properties.
 * @hw_queues_props: H/W queues properties.
 * @armcp_info: received various information from ArmCP regarding the H/W, e.g.
 *		available sensors.
 * @uboot_ver: F/W U-boot version.
 * @preboot_ver: F/W Preboot version.
 * @dmmu: DRAM MMU address translation properties.
 * @pmmu: PCI (host) MMU address translation properties.
 * @pmmu_huge: PCI (host) MMU address translation properties for memory
 *              allocated with huge pages.
 * @sram_base_address: SRAM physical start address.
 * @sram_end_address: SRAM physical end address.
 * @sram_user_base_address - SRAM physical start address for user access.
 * @dram_base_address: DRAM physical start address.
 * @dram_end_address: DRAM physical end address.
 * @dram_user_base_address: DRAM physical start address for user access.
 * @dram_size: DRAM total size.
 * @dram_pci_bar_size: size of PCI bar towards DRAM.
 * @max_power_default: max power of the device after reset
 * @dram_size_for_default_page_mapping: DRAM size needed to map to avoid page
 *                                      fault.
 * @pcie_dbi_base_address: Base address of the PCIE_DBI block.
 * @pcie_aux_dbi_reg_addr: Address of the PCIE_AUX DBI register.
 * @mmu_pgt_addr: base physical address in DRAM of MMU page tables.
 * @mmu_dram_default_page_addr: DRAM default page physical address.
 * @mmu_pgt_size: MMU page tables total size.
 * @mmu_pte_size: PTE size in MMU page tables.
 * @mmu_hop_table_size: MMU hop table size.
 * @mmu_hop0_tables_total_size: total size of MMU hop0 tables.
 * @dram_page_size: page size for MMU DRAM allocation.
 * @cfg_size: configuration space size on SRAM.
 * @sram_size: total size of SRAM.
 * @max_asid: maximum number of open contexts (ASIDs).
 * @num_of_events: number of possible internal H/W IRQs.
 * @psoc_pci_pll_nr: PCI PLL NR value.
 * @psoc_pci_pll_nf: PCI PLL NF value.
 * @psoc_pci_pll_od: PCI PLL OD value.
 * @psoc_pci_pll_div_factor: PCI PLL DIV FACTOR 1 value.
 * @psoc_timestamp_frequency: frequency of the psoc timestamp clock.
 * @high_pll: high PLL frequency used by the device.
 * @cb_pool_cb_cnt: number of CBs in the CB pool.
 * @cb_pool_cb_size: size of each CB in the CB pool.
 * @max_pending_cs: maximum of concurrent pending command submissions
 * @max_queues: maximum amount of queues in the system
 * @sync_stream_first_sob: first sync object available for sync stream use
 * @sync_stream_first_mon: first monitor available for sync stream use
 * @tpc_enabled_mask: which TPCs are enabled.
 * @completion_queues_count: number of completion queues.
 */
struct asic_fixed_properties {
	struct hw_queue_properties	*hw_queues_props;
	struct armcp_info		armcp_info;
	char				uboot_ver[VERSION_MAX_LEN];
	char				preboot_ver[VERSION_MAX_LEN];
	struct hl_mmu_properties	dmmu;
	struct hl_mmu_properties	pmmu;
	struct hl_mmu_properties	pmmu_huge;
	u64				sram_base_address;
	u64				sram_end_address;
	u64				sram_user_base_address;
	u64				dram_base_address;
	u64				dram_end_address;
	u64				dram_user_base_address;
	u64				dram_size;
	u64				dram_pci_bar_size;
	u64				max_power_default;
	u64				dram_size_for_default_page_mapping;
	u64				pcie_dbi_base_address;
	u64				pcie_aux_dbi_reg_addr;
	u64				mmu_pgt_addr;
	u64				mmu_dram_default_page_addr;
	u32				mmu_pgt_size;
	u32				mmu_pte_size;
	u32				mmu_hop_table_size;
	u32				mmu_hop0_tables_total_size;
	u32				dram_page_size;
	u32				cfg_size;
	u32				sram_size;
	u32				max_asid;
	u32				num_of_events;
	u32				psoc_pci_pll_nr;
	u32				psoc_pci_pll_nf;
	u32				psoc_pci_pll_od;
	u32				psoc_pci_pll_div_factor;
	u32				psoc_timestamp_frequency;
	u32				high_pll;
	u32				cb_pool_cb_cnt;
	u32				cb_pool_cb_size;
	u32				max_pending_cs;
	u32				max_queues;
	u16				sync_stream_first_sob;
	u16				sync_stream_first_mon;
	u8				tpc_enabled_mask;
	u8				completion_queues_count;
};

/**
 * struct hl_cs_compl - command submission completion object.
 * @base_fence: kernel fence object.
 * @lock: spinlock to protect fence.
 * @hdev: habanalabs device structure.
 * @hw_sob: the H/W SOB used in this signal/wait CS.
 * @cs_seq: command submission sequence number.
 * @type: type of the CS - signal/wait.
 * @sob_val: the SOB value that is used in this signal/wait CS.
 */
struct hl_cs_compl {
	struct dma_fence	base_fence;
	spinlock_t		lock;
	struct hl_device	*hdev;
	struct hl_hw_sob	*hw_sob;
	u64			cs_seq;
	enum hl_cs_type		type;
	u16			sob_val;
};

/*
 * Command Buffers
 */

/**
 * struct hl_cb_mgr - describes a Command Buffer Manager.
 * @cb_lock: protects cb_handles.
 * @cb_handles: an idr to hold all command buffer handles.
 */
struct hl_cb_mgr {
	spinlock_t		cb_lock;
	struct idr		cb_handles; /* protected by cb_lock */
};

/**
 * struct hl_cb - describes a Command Buffer.
 * @refcount: reference counter for usage of the CB.
 * @hdev: pointer to device this CB belongs to.
 * @lock: spinlock to protect mmap/cs flows.
 * @debugfs_list: node in debugfs list of command buffers.
 * @pool_list: node in pool list of command buffers.
 * @kernel_address: Holds the CB's kernel virtual address.
 * @bus_address: Holds the CB's DMA address.
 * @mmap_size: Holds the CB's size that was mmaped.
 * @size: holds the CB's size.
 * @id: the CB's ID.
 * @cs_cnt: holds number of CS that this CB participates in.
 * @ctx_id: holds the ID of the owner's context.
 * @mmap: true if the CB is currently mmaped to user.
 * @is_pool: true if CB was acquired from the pool, false otherwise.
 * @is_internal: internaly allocated
 */
struct hl_cb {
	struct kref		refcount;
	struct hl_device	*hdev;
	spinlock_t		lock;
	struct list_head	debugfs_list;
	struct list_head	pool_list;
	u64			kernel_address;
	dma_addr_t		bus_address;
	u32			mmap_size;
	u32			size;
	u32			id;
	u32			cs_cnt;
	u32			ctx_id;
	u8			mmap;
	u8			is_pool;
	u8			is_internal;
};


/*
 * QUEUES
 */

struct hl_cs_job;

/* Queue length of external and HW queues */
#define HL_QUEUE_LENGTH			4096
#define HL_QUEUE_SIZE_IN_BYTES		(HL_QUEUE_LENGTH * HL_BD_SIZE)

#if (HL_MAX_JOBS_PER_CS > HL_QUEUE_LENGTH)
#error "HL_QUEUE_LENGTH must be greater than HL_MAX_JOBS_PER_CS"
#endif

/* HL_CQ_LENGTH is in units of struct hl_cq_entry */
#define HL_CQ_LENGTH			HL_QUEUE_LENGTH
#define HL_CQ_SIZE_IN_BYTES		(HL_CQ_LENGTH * HL_CQ_ENTRY_SIZE)

/* Must be power of 2 */
#define HL_EQ_LENGTH			64
#define HL_EQ_SIZE_IN_BYTES		(HL_EQ_LENGTH * HL_EQ_ENTRY_SIZE)

/* Host <-> ArmCP shared memory size */
#define HL_CPU_ACCESSIBLE_MEM_SIZE	SZ_2M

/**
 * struct hl_hw_queue - describes a H/W transport queue.
 * @hw_sob: array of the used H/W SOBs by this H/W queue.
 * @shadow_queue: pointer to a shadow queue that holds pointers to jobs.
 * @queue_type: type of queue.
 * @kernel_address: holds the queue's kernel virtual address.
 * @bus_address: holds the queue's DMA address.
 * @pi: holds the queue's pi value.
 * @ci: holds the queue's ci value, AS CALCULATED BY THE DRIVER (not real ci).
 * @hw_queue_id: the id of the H/W queue.
 * @cq_id: the id for the corresponding CQ for this H/W queue.
 * @msi_vec: the IRQ number of the H/W queue.
 * @int_queue_len: length of internal queue (number of entries).
 * @next_sob_val: the next value to use for the currently used SOB.
 * @base_sob_id: the base SOB id of the SOBs used by this queue.
 * @base_mon_id: the base MON id of the MONs used by this queue.
 * @valid: is the queue valid (we have array of 32 queues, not all of them
 *         exist).
 * @curr_sob_offset: the id offset to the currently used SOB from the
 *                   HL_RSVD_SOBS that are being used by this queue.
 * @supports_sync_stream: True if queue supports sync stream
 */
struct hl_hw_queue {
	struct hl_hw_sob	hw_sob[HL_RSVD_SOBS];
	struct hl_cs_job	**shadow_queue;
	enum hl_queue_type	queue_type;
	u64			kernel_address;
	dma_addr_t		bus_address;
	u32			pi;
	atomic_t		ci;
	u32			hw_queue_id;
	u32			cq_id;
	u32			msi_vec;
	u16			int_queue_len;
	u16			next_sob_val;
	u16			base_sob_id;
	u16			base_mon_id;
	u8			valid;
	u8			curr_sob_offset;
	u8			supports_sync_stream;
};

/**
 * struct hl_cq - describes a completion queue
 * @hdev: pointer to the device structure
 * @kernel_address: holds the queue's kernel virtual address
 * @bus_address: holds the queue's DMA address
 * @cq_idx: completion queue index in array
 * @hw_queue_id: the id of the matching H/W queue
 * @ci: ci inside the queue
 * @pi: pi inside the queue
 * @free_slots_cnt: counter of free slots in queue
 */
struct hl_cq {
	struct hl_device	*hdev;
	u64			kernel_address;
	dma_addr_t		bus_address;
	u32			cq_idx;
	u32			hw_queue_id;
	u32			ci;
	u32			pi;
	atomic_t		free_slots_cnt;
};

/**
 * struct hl_eq - describes the event queue (single one per device)
 * @hdev: pointer to the device structure
 * @kernel_address: holds the queue's kernel virtual address
 * @bus_address: holds the queue's DMA address
 * @ci: ci inside the queue
 */
struct hl_eq {
	struct hl_device	*hdev;
	u64			kernel_address;
	dma_addr_t		bus_address;
	u32			ci;
};


/*
 * ASICs
 */

/**
 * enum hl_asic_type - supported ASIC types.
 * @ASIC_INVALID: Invalid ASIC type.
 * @ASIC_GOYA: Goya device.
 * @ASIC_GAUDI: Gaudi device.
 */
enum hl_asic_type {
	ASIC_INVALID,
	ASIC_GOYA,
	ASIC_GAUDI
};

struct hl_cs_parser;

/**
 * enum hl_pm_mng_profile - power management profile.
 * @PM_AUTO: internal clock is set by the Linux driver.
 * @PM_MANUAL: internal clock is set by the user.
 * @PM_LAST: last power management type.
 */
enum hl_pm_mng_profile {
	PM_AUTO = 1,
	PM_MANUAL,
	PM_LAST
};

/**
 * enum hl_pll_frequency - PLL frequency.
 * @PLL_HIGH: high frequency.
 * @PLL_LOW: low frequency.
 * @PLL_LAST: last frequency values that were configured by the user.
 */
enum hl_pll_frequency {
	PLL_HIGH = 1,
	PLL_LOW,
	PLL_LAST
};

#define PLL_REF_CLK 50

enum div_select_defs {
	DIV_SEL_REF_CLK = 0,
	DIV_SEL_PLL_CLK = 1,
	DIV_SEL_DIVIDED_REF = 2,
	DIV_SEL_DIVIDED_PLL = 3,
};

/**
 * struct hl_asic_funcs - ASIC specific functions that are can be called from
 *                        common code.
 * @early_init: sets up early driver state (pre sw_init), doesn't configure H/W.
 * @early_fini: tears down what was done in early_init.
 * @late_init: sets up late driver/hw state (post hw_init) - Optional.
 * @late_fini: tears down what was done in late_init (pre hw_fini) - Optional.
 * @sw_init: sets up driver state, does not configure H/W.
 * @sw_fini: tears down driver state, does not configure H/W.
 * @hw_init: sets up the H/W state.
 * @hw_fini: tears down the H/W state.
 * @halt_engines: halt engines, needed for reset sequence. This also disables
 *                interrupts from the device. Should be called before
 *                hw_fini and before CS rollback.
 * @suspend: handles IP specific H/W or SW changes for suspend.
 * @resume: handles IP specific H/W or SW changes for resume.
 * @cb_mmap: maps a CB.
 * @ring_doorbell: increment PI on a given QMAN.
 * @pqe_write: Write the PQ entry to the PQ. This is ASIC-specific
 *             function because the PQs are located in different memory areas
 *             per ASIC (SRAM, DRAM, Host memory) and therefore, the method of
 *             writing the PQE must match the destination memory area
 *             properties.
 * @asic_dma_alloc_coherent: Allocate coherent DMA memory by calling
 *                           dma_alloc_coherent(). This is ASIC function because
 *                           its implementation is not trivial when the driver
 *                           is loaded in simulation mode (not upstreamed).
 * @asic_dma_free_coherent:  Free coherent DMA memory by calling
 *                           dma_free_coherent(). This is ASIC function because
 *                           its implementation is not trivial when the driver
 *                           is loaded in simulation mode (not upstreamed).
 * @get_int_queue_base: get the internal queue base address.
 * @test_queues: run simple test on all queues for sanity check.
 * @asic_dma_pool_zalloc: small DMA allocation of coherent memory from DMA pool.
 *                        size of allocation is HL_DMA_POOL_BLK_SIZE.
 * @asic_dma_pool_free: free small DMA allocation from pool.
 * @cpu_accessible_dma_pool_alloc: allocate CPU PQ packet from DMA pool.
 * @cpu_accessible_dma_pool_free: free CPU PQ packet from DMA pool.
 * @hl_dma_unmap_sg: DMA unmap scatter-gather list.
 * @cs_parser: parse Command Submission.
 * @asic_dma_map_sg: DMA map scatter-gather list.
 * @get_dma_desc_list_size: get number of LIN_DMA packets required for CB.
 * @add_end_of_cb_packets: Add packets to the end of CB, if device requires it.
 * @update_eq_ci: update event queue CI.
 * @context_switch: called upon ASID context switch.
 * @restore_phase_topology: clear all SOBs amd MONs.
 * @debugfs_read32: debug interface for reading u32 from DRAM/SRAM.
 * @debugfs_write32: debug interface for writing u32 to DRAM/SRAM.
 * @add_device_attr: add ASIC specific device attributes.
 * @handle_eqe: handle event queue entry (IRQ) from ArmCP.
 * @set_pll_profile: change PLL profile (manual/automatic).
 * @get_events_stat: retrieve event queue entries histogram.
 * @read_pte: read MMU page table entry from DRAM.
 * @write_pte: write MMU page table entry to DRAM.
 * @mmu_invalidate_cache: flush MMU STLB host/DRAM cache, either with soft
 *                        (L1 only) or hard (L0 & L1) flush.
 * @mmu_invalidate_cache_range: flush specific MMU STLB cache lines with
 *                              ASID-VA-size mask.
 * @send_heartbeat: send is-alive packet to ArmCP and verify response.
 * @set_clock_gating: enable/disable clock gating per engine according to
 *                    clock gating mask in hdev
 * @disable_clock_gating: disable clock gating completely
 * @debug_coresight: perform certain actions on Coresight for debugging.
 * @is_device_idle: return true if device is idle, false otherwise.
 * @soft_reset_late_init: perform certain actions needed after soft reset.
 * @hw_queues_lock: acquire H/W queues lock.
 * @hw_queues_unlock: release H/W queues lock.
 * @get_pci_id: retrieve PCI ID.
 * @get_eeprom_data: retrieve EEPROM data from F/W.
 * @send_cpu_message: send message to F/W. If the message is timedout, the
 *                    driver will eventually reset the device. The timeout can
 *                    be determined by the calling function or it can be 0 and
 *                    then the timeout is the default timeout for the specific
 *                    ASIC
 * @get_hw_state: retrieve the H/W state
 * @pci_bars_map: Map PCI BARs.
 * @set_dram_bar_base: Set DRAM BAR to map specific device address. Returns
 *                     old address the bar pointed to or U64_MAX for failure
 * @init_iatu: Initialize the iATU unit inside the PCI controller.
 * @rreg: Read a register. Needed for simulator support.
 * @wreg: Write a register. Needed for simulator support.
 * @halt_coresight: stop the ETF and ETR traces.
 * @ctx_init: context dependent initialization.
 * @get_clk_rate: Retrieve the ASIC current and maximum clock rate in MHz
 * @get_queue_id_for_cq: Get the H/W queue id related to the given CQ index.
 * @read_device_fw_version: read the device's firmware versions that are
 *                          contained in registers
 * @load_firmware_to_device: load the firmware to the device's memory
 * @load_boot_fit_to_device: load boot fit to device's memory
 * @get_signal_cb_size: Get signal CB size.
 * @get_wait_cb_size: Get wait CB size.
 * @gen_signal_cb: Generate a signal CB.
 * @gen_wait_cb: Generate a wait CB.
 * @reset_sob: Reset a SOB.
 * @set_dma_mask_from_fw: set the DMA mask in the driver according to the
 *                        firmware configuration
 * @get_device_time: Get the device time.
 */
struct hl_asic_funcs {
	int (*early_init)(struct hl_device *hdev);
	int (*early_fini)(struct hl_device *hdev);
	int (*late_init)(struct hl_device *hdev);
	void (*late_fini)(struct hl_device *hdev);
	int (*sw_init)(struct hl_device *hdev);
	int (*sw_fini)(struct hl_device *hdev);
	int (*hw_init)(struct hl_device *hdev);
	void (*hw_fini)(struct hl_device *hdev, bool hard_reset);
	void (*halt_engines)(struct hl_device *hdev, bool hard_reset);
	int (*suspend)(struct hl_device *hdev);
	int (*resume)(struct hl_device *hdev);
	int (*cb_mmap)(struct hl_device *hdev, struct vm_area_struct *vma,
			u64 kaddress, phys_addr_t paddress, u32 size);
	void (*ring_doorbell)(struct hl_device *hdev, u32 hw_queue_id, u32 pi);
	void (*pqe_write)(struct hl_device *hdev, __le64 *pqe,
			struct hl_bd *bd);
	void* (*asic_dma_alloc_coherent)(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag);
	void (*asic_dma_free_coherent)(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle);
	void* (*get_int_queue_base)(struct hl_device *hdev, u32 queue_id,
				dma_addr_t *dma_handle, u16 *queue_len);
	int (*test_queues)(struct hl_device *hdev);
	void* (*asic_dma_pool_zalloc)(struct hl_device *hdev, size_t size,
				gfp_t mem_flags, dma_addr_t *dma_handle);
	void (*asic_dma_pool_free)(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr);
	void* (*cpu_accessible_dma_pool_alloc)(struct hl_device *hdev,
				size_t size, dma_addr_t *dma_handle);
	void (*cpu_accessible_dma_pool_free)(struct hl_device *hdev,
				size_t size, void *vaddr);
	void (*hl_dma_unmap_sg)(struct hl_device *hdev,
				struct scatterlist *sgl, int nents,
				enum dma_data_direction dir);
	int (*cs_parser)(struct hl_device *hdev, struct hl_cs_parser *parser);
	int (*asic_dma_map_sg)(struct hl_device *hdev,
				struct scatterlist *sgl, int nents,
				enum dma_data_direction dir);
	u32 (*get_dma_desc_list_size)(struct hl_device *hdev,
					struct sg_table *sgt);
	void (*add_end_of_cb_packets)(struct hl_device *hdev,
					u64 kernel_address, u32 len,
					u64 cq_addr, u32 cq_val, u32 msix_num,
					bool eb);
	void (*update_eq_ci)(struct hl_device *hdev, u32 val);
	int (*context_switch)(struct hl_device *hdev, u32 asid);
	void (*restore_phase_topology)(struct hl_device *hdev);
	int (*debugfs_read32)(struct hl_device *hdev, u64 addr, u32 *val);
	int (*debugfs_write32)(struct hl_device *hdev, u64 addr, u32 val);
	int (*debugfs_read64)(struct hl_device *hdev, u64 addr, u64 *val);
	int (*debugfs_write64)(struct hl_device *hdev, u64 addr, u64 val);
	void (*add_device_attr)(struct hl_device *hdev,
				struct attribute_group *dev_attr_grp);
	void (*handle_eqe)(struct hl_device *hdev,
				struct hl_eq_entry *eq_entry);
	void (*set_pll_profile)(struct hl_device *hdev,
			enum hl_pll_frequency freq);
	void* (*get_events_stat)(struct hl_device *hdev, bool aggregate,
				u32 *size);
	u64 (*read_pte)(struct hl_device *hdev, u64 addr);
	void (*write_pte)(struct hl_device *hdev, u64 addr, u64 val);
	int (*mmu_invalidate_cache)(struct hl_device *hdev, bool is_hard,
					u32 flags);
	int (*mmu_invalidate_cache_range)(struct hl_device *hdev, bool is_hard,
			u32 asid, u64 va, u64 size);
	int (*send_heartbeat)(struct hl_device *hdev);
	void (*set_clock_gating)(struct hl_device *hdev);
	void (*disable_clock_gating)(struct hl_device *hdev);
	int (*debug_coresight)(struct hl_device *hdev, void *data);
	bool (*is_device_idle)(struct hl_device *hdev, u32 *mask,
				struct seq_file *s);
	int (*soft_reset_late_init)(struct hl_device *hdev);
	void (*hw_queues_lock)(struct hl_device *hdev);
	void (*hw_queues_unlock)(struct hl_device *hdev);
	u32 (*get_pci_id)(struct hl_device *hdev);
	int (*get_eeprom_data)(struct hl_device *hdev, void *data,
				size_t max_size);
	int (*send_cpu_message)(struct hl_device *hdev, u32 *msg,
				u16 len, u32 timeout, long *result);
	enum hl_device_hw_state (*get_hw_state)(struct hl_device *hdev);
	int (*pci_bars_map)(struct hl_device *hdev);
	u64 (*set_dram_bar_base)(struct hl_device *hdev, u64 addr);
	int (*init_iatu)(struct hl_device *hdev);
	u32 (*rreg)(struct hl_device *hdev, u32 reg);
	void (*wreg)(struct hl_device *hdev, u32 reg, u32 val);
	void (*halt_coresight)(struct hl_device *hdev);
	int (*ctx_init)(struct hl_ctx *ctx);
	int (*get_clk_rate)(struct hl_device *hdev, u32 *cur_clk, u32 *max_clk);
	u32 (*get_queue_id_for_cq)(struct hl_device *hdev, u32 cq_idx);
	void (*read_device_fw_version)(struct hl_device *hdev,
					enum hl_fw_component fwc);
	int (*load_firmware_to_device)(struct hl_device *hdev);
	int (*load_boot_fit_to_device)(struct hl_device *hdev);
	u32 (*get_signal_cb_size)(struct hl_device *hdev);
	u32 (*get_wait_cb_size)(struct hl_device *hdev);
	void (*gen_signal_cb)(struct hl_device *hdev, void *data, u16 sob_id);
	void (*gen_wait_cb)(struct hl_device *hdev, void *data, u16 sob_id,
				u16 sob_val, u16 mon_id, u32 q_idx);
	void (*reset_sob)(struct hl_device *hdev, void *data);
	void (*set_dma_mask_from_fw)(struct hl_device *hdev);
	u64 (*get_device_time)(struct hl_device *hdev);
};


/*
 * CONTEXTS
 */

#define HL_KERNEL_ASID_ID	0

/**
 * struct hl_va_range - virtual addresses range.
 * @lock: protects the virtual addresses list.
 * @list: list of virtual addresses blocks available for mappings.
 * @start_addr: range start address.
 * @end_addr: range end address.
 */
struct hl_va_range {
	struct mutex		lock;
	struct list_head	list;
	u64			start_addr;
	u64			end_addr;
};

/**
 * struct hl_ctx - user/kernel context.
 * @mem_hash: holds mapping from virtual address to virtual memory area
 *		descriptor (hl_vm_phys_pg_list or hl_userptr).
 * @mmu_shadow_hash: holds a mapping from shadow address to pgt_info structure.
 * @hpriv: pointer to the private (Kernel Driver) data of the process (fd).
 * @hdev: pointer to the device structure.
 * @refcount: reference counter for the context. Context is released only when
 *		this hits 0l. It is incremented on CS and CS_WAIT.
 * @cs_pending: array of DMA fence objects representing pending CS.
 * @host_va_range: holds available virtual addresses for host mappings.
 * @host_huge_va_range: holds available virtual addresses for host mappings
 *                      with huge pages.
 * @dram_va_range: holds available virtual addresses for DRAM mappings.
 * @mem_hash_lock: protects the mem_hash.
 * @mmu_lock: protects the MMU page tables. Any change to the PGT, modifying the
 *            MMU hash or walking the PGT requires talking this lock.
 * @debugfs_list: node in debugfs list of contexts.
 * @cs_sequence: sequence number for CS. Value is assigned to a CS and passed
 *			to user so user could inquire about CS. It is used as
 *			index to cs_pending array.
 * @dram_default_hops: array that holds all hops addresses needed for default
 *                     DRAM mapping.
 * @cs_lock: spinlock to protect cs_sequence.
 * @dram_phys_mem: amount of used physical DRAM memory by this context.
 * @thread_ctx_switch_token: token to prevent multiple threads of the same
 *				context	from running the context switch phase.
 *				Only a single thread should run it.
 * @thread_ctx_switch_wait_token: token to prevent the threads that didn't run
 *				the context switch phase from moving to their
 *				execution phase before the context switch phase
 *				has finished.
 * @asid: context's unique address space ID in the device's MMU.
 * @handle: context's opaque handle for user
 */
struct hl_ctx {
	DECLARE_HASHTABLE(mem_hash, MEM_HASH_TABLE_BITS);
	DECLARE_HASHTABLE(mmu_shadow_hash, MMU_HASH_TABLE_BITS);
	struct hl_fpriv		*hpriv;
	struct hl_device	*hdev;
	struct kref		refcount;
	struct dma_fence	**cs_pending;
	struct hl_va_range	*host_va_range;
	struct hl_va_range	*host_huge_va_range;
	struct hl_va_range	*dram_va_range;
	struct mutex		mem_hash_lock;
	struct mutex		mmu_lock;
	struct list_head	debugfs_list;
	struct hl_cs_counters	cs_counters;
	u64			cs_sequence;
	u64			*dram_default_hops;
	spinlock_t		cs_lock;
	atomic64_t		dram_phys_mem;
	atomic_t		thread_ctx_switch_token;
	u32			thread_ctx_switch_wait_token;
	u32			asid;
	u32			handle;
};

/**
 * struct hl_ctx_mgr - for handling multiple contexts.
 * @ctx_lock: protects ctx_handles.
 * @ctx_handles: idr to hold all ctx handles.
 */
struct hl_ctx_mgr {
	struct mutex		ctx_lock;
	struct idr		ctx_handles;
};



/*
 * COMMAND SUBMISSIONS
 */

/**
 * struct hl_userptr - memory mapping chunk information
 * @vm_type: type of the VM.
 * @job_node: linked-list node for hanging the object on the Job's list.
 * @vec: pointer to the frame vector.
 * @sgt: pointer to the scatter-gather table that holds the pages.
 * @dir: for DMA unmapping, the direction must be supplied, so save it.
 * @debugfs_list: node in debugfs list of command submissions.
 * @addr: user-space virtual address of the start of the memory area.
 * @size: size of the memory area to pin & map.
 * @dma_mapped: true if the SG was mapped to DMA addresses, false otherwise.
 */
struct hl_userptr {
	enum vm_type_t		vm_type; /* must be first */
	struct list_head	job_node;
	struct frame_vector	*vec;
	struct sg_table		*sgt;
	enum dma_data_direction dir;
	struct list_head	debugfs_list;
	u64			addr;
	u32			size;
	u8			dma_mapped;
};

/**
 * struct hl_cs - command submission.
 * @jobs_in_queue_cnt: per each queue, maintain counter of submitted jobs.
 * @ctx: the context this CS belongs to.
 * @job_list: list of the CS's jobs in the various queues.
 * @job_lock: spinlock for the CS's jobs list. Needed for free_job.
 * @refcount: reference counter for usage of the CS.
 * @fence: pointer to the fence object of this CS.
 * @signal_fence: pointer to the fence object of the signal CS (used by wait
 *                CS only).
 * @finish_work: workqueue object to run when CS is completed by H/W.
 * @work_tdr: delayed work node for TDR.
 * @mirror_node : node in device mirror list of command submissions.
 * @debugfs_list: node in debugfs list of command submissions.
 * @sequence: the sequence number of this CS.
 * @type: CS_TYPE_*.
 * @submitted: true if CS was submitted to H/W.
 * @completed: true if CS was completed by device.
 * @timedout : true if CS was timedout.
 * @tdr_active: true if TDR was activated for this CS (to prevent
 *		double TDR activation).
 * @aborted: true if CS was aborted due to some device error.
 */
struct hl_cs {
	u16			*jobs_in_queue_cnt;
	struct hl_ctx		*ctx;
	struct list_head	job_list;
	spinlock_t		job_lock;
	struct kref		refcount;
	struct dma_fence	*fence;
	struct dma_fence	*signal_fence;
	struct work_struct	finish_work;
	struct delayed_work	work_tdr;
	struct list_head	mirror_node;
	struct list_head	debugfs_list;
	u64			sequence;
	enum hl_cs_type		type;
	u8			submitted;
	u8			completed;
	u8			timedout;
	u8			tdr_active;
	u8			aborted;
};

/**
 * struct hl_cs_job - command submission job.
 * @cs_node: the node to hang on the CS jobs list.
 * @cs: the CS this job belongs to.
 * @user_cb: the CB we got from the user.
 * @patched_cb: in case of patching, this is internal CB which is submitted on
 *		the queue instead of the CB we got from the IOCTL.
 * @finish_work: workqueue object to run when job is completed.
 * @userptr_list: linked-list of userptr mappings that belong to this job and
 *			wait for completion.
 * @debugfs_list: node in debugfs list of command submission jobs.
 * @queue_type: the type of the H/W queue this job is submitted to.
 * @id: the id of this job inside a CS.
 * @hw_queue_id: the id of the H/W queue this job is submitted to.
 * @user_cb_size: the actual size of the CB we got from the user.
 * @job_cb_size: the actual size of the CB that we put on the queue.
 * @is_kernel_allocated_cb: true if the CB handle we got from the user holds a
 *                          handle to a kernel-allocated CB object, false
 *                          otherwise (SRAM/DRAM/host address).
 * @contains_dma_pkt: whether the JOB contains at least one DMA packet. This
 *                    info is needed later, when adding the 2xMSG_PROT at the
 *                    end of the JOB, to know which barriers to put in the
 *                    MSG_PROT packets. Relevant only for GAUDI as GOYA doesn't
 *                    have streams so the engine can't be busy by another
 *                    stream.
 */
struct hl_cs_job {
	struct list_head	cs_node;
	struct hl_cs		*cs;
	struct hl_cb		*user_cb;
	struct hl_cb		*patched_cb;
	struct work_struct	finish_work;
	struct list_head	userptr_list;
	struct list_head	debugfs_list;
	enum hl_queue_type	queue_type;
	u32			id;
	u32			hw_queue_id;
	u32			user_cb_size;
	u32			job_cb_size;
	u8			is_kernel_allocated_cb;
	u8			contains_dma_pkt;
};

/**
 * struct hl_cs_parser - command submission parser properties.
 * @user_cb: the CB we got from the user.
 * @patched_cb: in case of patching, this is internal CB which is submitted on
 *		the queue instead of the CB we got from the IOCTL.
 * @job_userptr_list: linked-list of userptr mappings that belong to the related
 *			job and wait for completion.
 * @cs_sequence: the sequence number of the related CS.
 * @queue_type: the type of the H/W queue this job is submitted to.
 * @ctx_id: the ID of the context the related CS belongs to.
 * @hw_queue_id: the id of the H/W queue this job is submitted to.
 * @user_cb_size: the actual size of the CB we got from the user.
 * @patched_cb_size: the size of the CB after parsing.
 * @job_id: the id of the related job inside the related CS.
 * @is_kernel_allocated_cb: true if the CB handle we got from the user holds a
 *                          handle to a kernel-allocated CB object, false
 *                          otherwise (SRAM/DRAM/host address).
 * @contains_dma_pkt: whether the JOB contains at least one DMA packet. This
 *                    info is needed later, when adding the 2xMSG_PROT at the
 *                    end of the JOB, to know which barriers to put in the
 *                    MSG_PROT packets. Relevant only for GAUDI as GOYA doesn't
 *                    have streams so the engine can't be busy by another
 *                    stream.
 */
struct hl_cs_parser {
	struct hl_cb		*user_cb;
	struct hl_cb		*patched_cb;
	struct list_head	*job_userptr_list;
	u64			cs_sequence;
	enum hl_queue_type	queue_type;
	u32			ctx_id;
	u32			hw_queue_id;
	u32			user_cb_size;
	u32			patched_cb_size;
	u8			job_id;
	u8			is_kernel_allocated_cb;
	u8			contains_dma_pkt;
};


/*
 * MEMORY STRUCTURE
 */

/**
 * struct hl_vm_hash_node - hash element from virtual address to virtual
 *				memory area descriptor (hl_vm_phys_pg_list or
 *				hl_userptr).
 * @node: node to hang on the hash table in context object.
 * @vaddr: key virtual address.
 * @ptr: value pointer (hl_vm_phys_pg_list or hl_userptr).
 */
struct hl_vm_hash_node {
	struct hlist_node	node;
	u64			vaddr;
	void			*ptr;
};

/**
 * struct hl_vm_phys_pg_pack - physical page pack.
 * @vm_type: describes the type of the virtual area descriptor.
 * @pages: the physical page array.
 * @npages: num physical pages in the pack.
 * @total_size: total size of all the pages in this list.
 * @mapping_cnt: number of shared mappings.
 * @asid: the context related to this list.
 * @page_size: size of each page in the pack.
 * @flags: HL_MEM_* flags related to this list.
 * @handle: the provided handle related to this list.
 * @offset: offset from the first page.
 * @contiguous: is contiguous physical memory.
 * @created_from_userptr: is product of host virtual address.
 */
struct hl_vm_phys_pg_pack {
	enum vm_type_t		vm_type; /* must be first */
	u64			*pages;
	u64			npages;
	u64			total_size;
	atomic_t		mapping_cnt;
	u32			asid;
	u32			page_size;
	u32			flags;
	u32			handle;
	u32			offset;
	u8			contiguous;
	u8			created_from_userptr;
};

/**
 * struct hl_vm_va_block - virtual range block information.
 * @node: node to hang on the virtual range list in context object.
 * @start: virtual range start address.
 * @end: virtual range end address.
 * @size: virtual range size.
 */
struct hl_vm_va_block {
	struct list_head	node;
	u64			start;
	u64			end;
	u64			size;
};

/**
 * struct hl_vm - virtual memory manager for MMU.
 * @dram_pg_pool: pool for DRAM physical pages of 2MB.
 * @dram_pg_pool_refcount: reference counter for the pool usage.
 * @idr_lock: protects the phys_pg_list_handles.
 * @phys_pg_pack_handles: idr to hold all device allocations handles.
 * @init_done: whether initialization was done. We need this because VM
 *		initialization might be skipped during device initialization.
 */
struct hl_vm {
	struct gen_pool		*dram_pg_pool;
	struct kref		dram_pg_pool_refcount;
	spinlock_t		idr_lock;
	struct idr		phys_pg_pack_handles;
	u8			init_done;
};


/*
 * DEBUG, PROFILING STRUCTURE
 */

/**
 * struct hl_debug_params - Coresight debug parameters.
 * @input: pointer to component specific input parameters.
 * @output: pointer to component specific output parameters.
 * @output_size: size of output buffer.
 * @reg_idx: relevant register ID.
 * @op: component operation to execute.
 * @enable: true if to enable component debugging, false otherwise.
 */
struct hl_debug_params {
	void *input;
	void *output;
	u32 output_size;
	u32 reg_idx;
	u32 op;
	bool enable;
};

/*
 * FILE PRIVATE STRUCTURE
 */

/**
 * struct hl_fpriv - process information stored in FD private data.
 * @hdev: habanalabs device structure.
 * @filp: pointer to the given file structure.
 * @taskpid: current process ID.
 * @ctx: current executing context. TODO: remove for multiple ctx per process
 * @ctx_mgr: context manager to handle multiple context for this FD.
 * @cb_mgr: command buffer manager to handle multiple buffers for this FD.
 * @debugfs_list: list of relevant ASIC debugfs.
 * @dev_node: node in the device list of file private data
 * @refcount: number of related contexts.
 * @restore_phase_mutex: lock for context switch and restore phase.
 * @is_control: true for control device, false otherwise
 */
struct hl_fpriv {
	struct hl_device	*hdev;
	struct file		*filp;
	struct pid		*taskpid;
	struct hl_ctx		*ctx;
	struct hl_ctx_mgr	ctx_mgr;
	struct hl_cb_mgr	cb_mgr;
	struct list_head	debugfs_list;
	struct list_head	dev_node;
	struct kref		refcount;
	struct mutex		restore_phase_mutex;
	u8			is_control;
};


/*
 * DebugFS
 */

/**
 * struct hl_info_list - debugfs file ops.
 * @name: file name.
 * @show: function to output information.
 * @write: function to write to the file.
 */
struct hl_info_list {
	const char	*name;
	int		(*show)(struct seq_file *s, void *data);
	ssize_t		(*write)(struct file *file, const char __user *buf,
				size_t count, loff_t *f_pos);
};

/**
 * struct hl_debugfs_entry - debugfs dentry wrapper.
 * @dent: base debugfs entry structure.
 * @info_ent: dentry realted ops.
 * @dev_entry: ASIC specific debugfs manager.
 */
struct hl_debugfs_entry {
	struct dentry			*dent;
	const struct hl_info_list	*info_ent;
	struct hl_dbg_device_entry	*dev_entry;
};

/**
 * struct hl_dbg_device_entry - ASIC specific debugfs manager.
 * @root: root dentry.
 * @hdev: habanalabs device structure.
 * @entry_arr: array of available hl_debugfs_entry.
 * @file_list: list of available debugfs files.
 * @file_mutex: protects file_list.
 * @cb_list: list of available CBs.
 * @cb_spinlock: protects cb_list.
 * @cs_list: list of available CSs.
 * @cs_spinlock: protects cs_list.
 * @cs_job_list: list of available CB jobs.
 * @cs_job_spinlock: protects cs_job_list.
 * @userptr_list: list of available userptrs (virtual memory chunk descriptor).
 * @userptr_spinlock: protects userptr_list.
 * @ctx_mem_hash_list: list of available contexts with MMU mappings.
 * @ctx_mem_hash_spinlock: protects cb_list.
 * @addr: next address to read/write from/to in read/write32.
 * @mmu_addr: next virtual address to translate to physical address in mmu_show.
 * @mmu_asid: ASID to use while translating in mmu_show.
 * @i2c_bus: generic u8 debugfs file for bus value to use in i2c_data_read.
 * @i2c_bus: generic u8 debugfs file for address value to use in i2c_data_read.
 * @i2c_bus: generic u8 debugfs file for register value to use in i2c_data_read.
 */
struct hl_dbg_device_entry {
	struct dentry			*root;
	struct hl_device		*hdev;
	struct hl_debugfs_entry		*entry_arr;
	struct list_head		file_list;
	struct mutex			file_mutex;
	struct list_head		cb_list;
	spinlock_t			cb_spinlock;
	struct list_head		cs_list;
	spinlock_t			cs_spinlock;
	struct list_head		cs_job_list;
	spinlock_t			cs_job_spinlock;
	struct list_head		userptr_list;
	spinlock_t			userptr_spinlock;
	struct list_head		ctx_mem_hash_list;
	spinlock_t			ctx_mem_hash_spinlock;
	u64				addr;
	u64				mmu_addr;
	u32				mmu_asid;
	u8				i2c_bus;
	u8				i2c_addr;
	u8				i2c_reg;
};


/*
 * DEVICES
 */

/* Theoretical limit only. A single host can only contain up to 4 or 8 PCIe
 * x16 cards. In extreme cases, there are hosts that can accommodate 16 cards.
 */
#define HL_MAX_MINORS	256

/*
 * Registers read & write functions.
 */

u32 hl_rreg(struct hl_device *hdev, u32 reg);
void hl_wreg(struct hl_device *hdev, u32 reg, u32 val);

#define RREG32(reg) hdev->asic_funcs->rreg(hdev, (reg))
#define WREG32(reg, v) hdev->asic_funcs->wreg(hdev, (reg), (v))
#define DREG32(reg) pr_info("REGISTER: " #reg " : 0x%08X\n",	\
			hdev->asic_funcs->rreg(hdev, (reg)))

#define WREG32_P(reg, val, mask)				\
	do {							\
		u32 tmp_ = RREG32(reg);				\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32(reg, tmp_);				\
	} while (0)
#define WREG32_AND(reg, and) WREG32_P(reg, 0, and)
#define WREG32_OR(reg, or) WREG32_P(reg, or, ~(or))

#define RMWREG32(reg, val, mask)				\
	do {							\
		u32 tmp_ = RREG32(reg);				\
		tmp_ &= ~(mask);				\
		tmp_ |= ((val) << __ffs(mask));			\
		WREG32(reg, tmp_);				\
	} while (0)

#define RREG32_MASK(reg, mask) ((RREG32(reg) & mask) >> __ffs(mask))

#define REG_FIELD_SHIFT(reg, field) reg##_##field##_SHIFT
#define REG_FIELD_MASK(reg, field) reg##_##field##_MASK
#define WREG32_FIELD(reg, offset, field, val)	\
	WREG32(mm##reg + offset, (RREG32(mm##reg + offset) & \
				~REG_FIELD_MASK(reg, field)) | \
				(val) << REG_FIELD_SHIFT(reg, field))

/* Timeout should be longer when working with simulator but cap the
 * increased timeout to some maximum
 */
#define hl_poll_timeout(hdev, addr, val, cond, sleep_us, timeout_us) \
({ \
	ktime_t __timeout; \
	if (hdev->pdev) \
		__timeout = ktime_add_us(ktime_get(), timeout_us); \
	else \
		__timeout = ktime_add_us(ktime_get(),\
				min((u64)(timeout_us * 10), \
					(u64) HL_SIM_MAX_TIMEOUT_US)); \
	might_sleep_if(sleep_us); \
	for (;;) { \
		(val) = RREG32(addr); \
		if (cond) \
			break; \
		if (timeout_us && ktime_compare(ktime_get(), __timeout) > 0) { \
			(val) = RREG32(addr); \
			break; \
		} \
		if (sleep_us) \
			usleep_range((sleep_us >> 2) + 1, sleep_us); \
	} \
	(cond) ? 0 : -ETIMEDOUT; \
})

/*
 * address in this macro points always to a memory location in the
 * host's (server's) memory. That location is updated asynchronously
 * either by the direct access of the device or by another core.
 *
 * To work both in LE and BE architectures, we need to distinguish between the
 * two states (device or another core updates the memory location). Therefore,
 * if mem_written_by_device is true, the host memory being polled will be
 * updated directly by the device. If false, the host memory being polled will
 * be updated by host CPU. Required so host knows whether or not the memory
 * might need to be byte-swapped before returning value to caller.
 */
#define hl_poll_timeout_memory(hdev, addr, val, cond, sleep_us, timeout_us, \
				mem_written_by_device) \
({ \
	ktime_t __timeout; \
	if (hdev->pdev) \
		__timeout = ktime_add_us(ktime_get(), timeout_us); \
	else \
		__timeout = ktime_add_us(ktime_get(),\
				min((u64)(timeout_us * 10), \
					(u64) HL_SIM_MAX_TIMEOUT_US)); \
	might_sleep_if(sleep_us); \
	for (;;) { \
		/* Verify we read updates done by other cores or by device */ \
		mb(); \
		(val) = *((u32 *) (uintptr_t) (addr)); \
		if (mem_written_by_device) \
			(val) = le32_to_cpu(*(__le32 *) &(val)); \
		if (cond) \
			break; \
		if (timeout_us && ktime_compare(ktime_get(), __timeout) > 0) { \
			(val) = *((u32 *) (uintptr_t) (addr)); \
			if (mem_written_by_device) \
				(val) = le32_to_cpu(*(__le32 *) &(val)); \
			break; \
		} \
		if (sleep_us) \
			usleep_range((sleep_us >> 2) + 1, sleep_us); \
	} \
	(cond) ? 0 : -ETIMEDOUT; \
})

#define hl_poll_timeout_device_memory(hdev, addr, val, cond, sleep_us, \
					timeout_us) \
({ \
	ktime_t __timeout; \
	if (hdev->pdev) \
		__timeout = ktime_add_us(ktime_get(), timeout_us); \
	else \
		__timeout = ktime_add_us(ktime_get(),\
				min((u64)(timeout_us * 10), \
					(u64) HL_SIM_MAX_TIMEOUT_US)); \
	might_sleep_if(sleep_us); \
	for (;;) { \
		(val) = readl(addr); \
		if (cond) \
			break; \
		if (timeout_us && ktime_compare(ktime_get(), __timeout) > 0) { \
			(val) = readl(addr); \
			break; \
		} \
		if (sleep_us) \
			usleep_range((sleep_us >> 2) + 1, sleep_us); \
	} \
	(cond) ? 0 : -ETIMEDOUT; \
})

struct hwmon_chip_info;

/**
 * struct hl_device_reset_work - reset workqueue task wrapper.
 * @reset_work: reset work to be done.
 * @hdev: habanalabs device structure.
 */
struct hl_device_reset_work {
	struct work_struct		reset_work;
	struct hl_device		*hdev;
};

/**
 * struct hl_device_idle_busy_ts - used for calculating device utilization rate.
 * @idle_to_busy_ts: timestamp where device changed from idle to busy.
 * @busy_to_idle_ts: timestamp where device changed from busy to idle.
 */
struct hl_device_idle_busy_ts {
	ktime_t				idle_to_busy_ts;
	ktime_t				busy_to_idle_ts;
};

/**
 * struct hl_device - habanalabs device structure.
 * @pdev: pointer to PCI device, can be NULL in case of simulator device.
 * @pcie_bar_phys: array of available PCIe bars physical addresses.
 *		   (required only for PCI address match mode)
 * @pcie_bar: array of available PCIe bars virtual addresses.
 * @rmmio: configuration area address on SRAM.
 * @cdev: related char device.
 * @cdev_ctrl: char device for control operations only (INFO IOCTL)
 * @dev: related kernel basic device structure.
 * @dev_ctrl: related kernel device structure for the control device
 * @work_freq: delayed work to lower device frequency if possible.
 * @work_heartbeat: delayed work for ArmCP is-alive check.
 * @asic_name: ASIC specific nmae.
 * @asic_type: ASIC specific type.
 * @completion_queue: array of hl_cq.
 * @cq_wq: work queues of completion queues for executing work in process
 *         context.
 * @eq_wq: work queue of event queue for executing work in process context.
 * @kernel_ctx: Kernel driver context structure.
 * @kernel_queues: array of hl_hw_queue.
 * @hw_queues_mirror_list: CS mirror list for TDR.
 * @hw_queues_mirror_lock: protects hw_queues_mirror_list.
 * @kernel_cb_mgr: command buffer manager for creating/destroying/handling CGs.
 * @event_queue: event queue for IRQ from ArmCP.
 * @dma_pool: DMA pool for small allocations.
 * @cpu_accessible_dma_mem: Host <-> ArmCP shared memory CPU address.
 * @cpu_accessible_dma_address: Host <-> ArmCP shared memory DMA address.
 * @cpu_accessible_dma_pool: Host <-> ArmCP shared memory pool.
 * @asid_bitmap: holds used/available ASIDs.
 * @asid_mutex: protects asid_bitmap.
 * @send_cpu_message_lock: enforces only one message in Host <-> ArmCP queue.
 * @debug_lock: protects critical section of setting debug mode for device
 * @asic_prop: ASIC specific immutable properties.
 * @asic_funcs: ASIC specific functions.
 * @asic_specific: ASIC specific information to use only from ASIC files.
 * @mmu_pgt_pool: pool of available MMU hops.
 * @vm: virtual memory manager for MMU.
 * @mmu_cache_lock: protects MMU cache invalidation as it can serve one context.
 * @mmu_shadow_hop0: shadow mapping of the MMU hop 0 zone.
 * @hwmon_dev: H/W monitor device.
 * @pm_mng_profile: current power management profile.
 * @hl_chip_info: ASIC's sensors information.
 * @hl_debugfs: device's debugfs manager.
 * @cb_pool: list of preallocated CBs.
 * @cb_pool_lock: protects the CB pool.
 * @internal_cb_pool_virt_addr: internal command buffer pool virtual address.
 * @internal_cb_pool_dma_addr: internal command buffer pool dma address.
 * @internal_cb_pool: internal command buffer memory pool.
 * @internal_cb_va_base: internal cb pool mmu virtual address base
 * @fpriv_list: list of file private data structures. Each structure is created
 *              when a user opens the device
 * @fpriv_list_lock: protects the fpriv_list
 * @compute_ctx: current compute context executing.
 * @idle_busy_ts_arr: array to hold time stamps of transitions from idle to busy
 *                    and vice-versa
 * @aggregated_cs_counters: aggregated cs counters among all contexts
 * @dram_used_mem: current DRAM memory consumption.
 * @timeout_jiffies: device CS timeout value.
 * @max_power: the max power of the device, as configured by the sysadmin. This
 *             value is saved so in case of hard-reset, the driver will restore
 *             this value and update the F/W after the re-initialization
 * @clock_gating_mask: is clock gating enabled. bitmask that represents the
 *                     different engines. See debugfs-driver-habanalabs for
 *                     details.
 * @in_reset: is device in reset flow.
 * @curr_pll_profile: current PLL profile.
 * @cs_active_cnt: number of active command submissions on this device (active
 *                 means already in H/W queues)
 * @major: habanalabs kernel driver major.
 * @high_pll: high PLL profile frequency.
 * @soft_reset_cnt: number of soft reset since the driver was loaded.
 * @hard_reset_cnt: number of hard reset since the driver was loaded.
 * @idle_busy_ts_idx: index of current entry in idle_busy_ts_arr
 * @id: device minor.
 * @id_control: minor of the control device
 * @cpu_pci_msb_addr: 50-bit extension bits for the device CPU's 40-bit
 *                    addresses.
 * @disabled: is device disabled.
 * @late_init_done: is late init stage was done during initialization.
 * @hwmon_initialized: is H/W monitor sensors was initialized.
 * @hard_reset_pending: is there a hard reset work pending.
 * @heartbeat: is heartbeat sanity check towards ArmCP enabled.
 * @reset_on_lockup: true if a reset should be done in case of stuck CS, false
 *                   otherwise.
 * @dram_supports_virtual_memory: is MMU enabled towards DRAM.
 * @dram_default_page_mapping: is DRAM default page mapping enabled.
 * @pmmu_huge_range: is a different virtual addresses range used for PMMU with
 *                   huge pages.
 * @init_done: is the initialization of the device done.
 * @mmu_enable: is MMU enabled.
 * @mmu_huge_page_opt: is MMU huge pages optimization enabled.
 * @device_cpu_disabled: is the device CPU disabled (due to timeouts)
 * @dma_mask: the dma mask that was set for this device
 * @in_debug: is device under debug. This, together with fpriv_list, enforces
 *            that only a single user is configuring the debug infrastructure.
 * @power9_64bit_dma_enable: true to enable 64-bit DMA mask support. Relevant
 *                           only to POWER9 machines.
 * @cdev_sysfs_created: were char devices and sysfs nodes created.
 * @stop_on_err: true if engines should stop on error.
 * @supports_sync_stream: is sync stream supported.
 * @sync_stream_queue_idx: helper index for sync stream queues initialization.
 * @supports_coresight: is CoreSight supported.
 * @supports_soft_reset: is soft reset supported.
 */
struct hl_device {
	struct pci_dev			*pdev;
	u64				pcie_bar_phys[HL_PCI_NUM_BARS];
	void __iomem			*pcie_bar[HL_PCI_NUM_BARS];
	void __iomem			*rmmio;
	struct cdev			cdev;
	struct cdev			cdev_ctrl;
	struct device			*dev;
	struct device			*dev_ctrl;
	struct delayed_work		work_freq;
	struct delayed_work		work_heartbeat;
	char				asic_name[16];
	enum hl_asic_type		asic_type;
	struct hl_cq			*completion_queue;
	struct workqueue_struct		**cq_wq;
	struct workqueue_struct		*eq_wq;
	struct hl_ctx			*kernel_ctx;
	struct hl_hw_queue		*kernel_queues;
	struct list_head		hw_queues_mirror_list;
	spinlock_t			hw_queues_mirror_lock;
	struct hl_cb_mgr		kernel_cb_mgr;
	struct hl_eq			event_queue;
	struct dma_pool			*dma_pool;
	void				*cpu_accessible_dma_mem;
	dma_addr_t			cpu_accessible_dma_address;
	struct gen_pool			*cpu_accessible_dma_pool;
	unsigned long			*asid_bitmap;
	struct mutex			asid_mutex;
	struct mutex			send_cpu_message_lock;
	struct mutex			debug_lock;
	struct asic_fixed_properties	asic_prop;
	const struct hl_asic_funcs	*asic_funcs;
	void				*asic_specific;
	struct gen_pool			*mmu_pgt_pool;
	struct hl_vm			vm;
	struct mutex			mmu_cache_lock;
	void				*mmu_shadow_hop0;
	struct device			*hwmon_dev;
	enum hl_pm_mng_profile		pm_mng_profile;
	struct hwmon_chip_info		*hl_chip_info;

	struct hl_dbg_device_entry	hl_debugfs;

	struct list_head		cb_pool;
	spinlock_t			cb_pool_lock;

	void				*internal_cb_pool_virt_addr;
	dma_addr_t			internal_cb_pool_dma_addr;
	struct gen_pool			*internal_cb_pool;
	u64				internal_cb_va_base;

	struct list_head		fpriv_list;
	struct mutex			fpriv_list_lock;

	struct hl_ctx			*compute_ctx;

	struct hl_device_idle_busy_ts	*idle_busy_ts_arr;

	struct hl_cs_counters		aggregated_cs_counters;

	atomic64_t			dram_used_mem;
	u64				timeout_jiffies;
	u64				max_power;
	u64				clock_gating_mask;
	atomic_t			in_reset;
	enum hl_pll_frequency		curr_pll_profile;
	int				cs_active_cnt;
	u32				major;
	u32				high_pll;
	u32				soft_reset_cnt;
	u32				hard_reset_cnt;
	u32				idle_busy_ts_idx;
	u16				id;
	u16				id_control;
	u16				cpu_pci_msb_addr;
	u8				disabled;
	u8				late_init_done;
	u8				hwmon_initialized;
	u8				hard_reset_pending;
	u8				heartbeat;
	u8				reset_on_lockup;
	u8				dram_supports_virtual_memory;
	u8				dram_default_page_mapping;
	u8				pmmu_huge_range;
	u8				init_done;
	u8				device_cpu_disabled;
	u8				dma_mask;
	u8				in_debug;
	u8				power9_64bit_dma_enable;
	u8				cdev_sysfs_created;
	u8				stop_on_err;
	u8				supports_sync_stream;
	u8				sync_stream_queue_idx;
	u8				supports_coresight;
	u8				supports_soft_reset;

	/* Parameters for bring-up */
	u8				mmu_enable;
	u8				mmu_huge_page_opt;
	u8				cpu_enable;
	u8				reset_pcilink;
	u8				cpu_queues_enable;
	u8				fw_loading;
	u8				pldm;
	u8				axi_drain;
	u8				sram_scrambler_enable;
	u8				dram_scrambler_enable;
	u8				hard_reset_on_fw_events;
	u8				bmc_enable;
	u8				rl_enable;
};


/*
 * IOCTLs
 */

/**
 * typedef hl_ioctl_t - typedef for ioctl function in the driver
 * @hpriv: pointer to the FD's private data, which contains state of
 *		user process
 * @data: pointer to the input/output arguments structure of the IOCTL
 *
 * Return: 0 for success, negative value for error
 */
typedef int hl_ioctl_t(struct hl_fpriv *hpriv, void *data);

/**
 * struct hl_ioctl_desc - describes an IOCTL entry of the driver.
 * @cmd: the IOCTL code as created by the kernel macros.
 * @func: pointer to the driver's function that should be called for this IOCTL.
 */
struct hl_ioctl_desc {
	unsigned int cmd;
	hl_ioctl_t *func;
};


/*
 * Kernel module functions that can be accessed by entire module
 */

/**
 * hl_mem_area_inside_range() - Checks whether address+size are inside a range.
 * @address: The start address of the area we want to validate.
 * @size: The size in bytes of the area we want to validate.
 * @range_start_address: The start address of the valid range.
 * @range_end_address: The end address of the valid range.
 *
 * Return: true if the area is inside the valid range, false otherwise.
 */
static inline bool hl_mem_area_inside_range(u64 address, u32 size,
				u64 range_start_address, u64 range_end_address)
{
	u64 end_address = address + size;

	if ((address >= range_start_address) &&
			(end_address <= range_end_address) &&
			(end_address > address))
		return true;

	return false;
}

/**
 * hl_mem_area_crosses_range() - Checks whether address+size crossing a range.
 * @address: The start address of the area we want to validate.
 * @size: The size in bytes of the area we want to validate.
 * @range_start_address: The start address of the valid range.
 * @range_end_address: The end address of the valid range.
 *
 * Return: true if the area overlaps part or all of the valid range,
 *		false otherwise.
 */
static inline bool hl_mem_area_crosses_range(u64 address, u32 size,
				u64 range_start_address, u64 range_end_address)
{
	u64 end_address = address + size;

	if ((address >= range_start_address) &&
			(address < range_end_address))
		return true;

	if ((end_address >= range_start_address) &&
			(end_address < range_end_address))
		return true;

	if ((address < range_start_address) &&
			(end_address >= range_end_address))
		return true;

	return false;
}

int hl_device_open(struct inode *inode, struct file *filp);
int hl_device_open_ctrl(struct inode *inode, struct file *filp);
bool hl_device_disabled_or_in_reset(struct hl_device *hdev);
enum hl_device_status hl_device_status(struct hl_device *hdev);
int hl_device_set_debug_mode(struct hl_device *hdev, bool enable);
int create_hdev(struct hl_device **dev, struct pci_dev *pdev,
		enum hl_asic_type asic_type, int minor);
void destroy_hdev(struct hl_device *hdev);
int hl_hw_queues_create(struct hl_device *hdev);
void hl_hw_queues_destroy(struct hl_device *hdev);
int hl_hw_queue_send_cb_no_cmpl(struct hl_device *hdev, u32 hw_queue_id,
				u32 cb_size, u64 cb_ptr);
int hl_hw_queue_schedule_cs(struct hl_cs *cs);
u32 hl_hw_queue_add_ptr(u32 ptr, u16 val);
void hl_hw_queue_inc_ci_kernel(struct hl_device *hdev, u32 hw_queue_id);
void hl_int_hw_queue_update_ci(struct hl_cs *cs);
void hl_hw_queue_reset(struct hl_device *hdev, bool hard_reset);

#define hl_queue_inc_ptr(p)		hl_hw_queue_add_ptr(p, 1)
#define hl_pi_2_offset(pi)		((pi) & (HL_QUEUE_LENGTH - 1))

int hl_cq_init(struct hl_device *hdev, struct hl_cq *q, u32 hw_queue_id);
void hl_cq_fini(struct hl_device *hdev, struct hl_cq *q);
int hl_eq_init(struct hl_device *hdev, struct hl_eq *q);
void hl_eq_fini(struct hl_device *hdev, struct hl_eq *q);
void hl_cq_reset(struct hl_device *hdev, struct hl_cq *q);
void hl_eq_reset(struct hl_device *hdev, struct hl_eq *q);
irqreturn_t hl_irq_handler_cq(int irq, void *arg);
irqreturn_t hl_irq_handler_eq(int irq, void *arg);
u32 hl_cq_inc_ptr(u32 ptr);

int hl_asid_init(struct hl_device *hdev);
void hl_asid_fini(struct hl_device *hdev);
unsigned long hl_asid_alloc(struct hl_device *hdev);
void hl_asid_free(struct hl_device *hdev, unsigned long asid);

int hl_ctx_create(struct hl_device *hdev, struct hl_fpriv *hpriv);
void hl_ctx_free(struct hl_device *hdev, struct hl_ctx *ctx);
int hl_ctx_init(struct hl_device *hdev, struct hl_ctx *ctx, bool is_kernel_ctx);
void hl_ctx_do_release(struct kref *ref);
void hl_ctx_get(struct hl_device *hdev,	struct hl_ctx *ctx);
int hl_ctx_put(struct hl_ctx *ctx);
struct dma_fence *hl_ctx_get_fence(struct hl_ctx *ctx, u64 seq);
void hl_ctx_mgr_init(struct hl_ctx_mgr *mgr);
void hl_ctx_mgr_fini(struct hl_device *hdev, struct hl_ctx_mgr *mgr);

int hl_device_init(struct hl_device *hdev, struct class *hclass);
void hl_device_fini(struct hl_device *hdev);
int hl_device_suspend(struct hl_device *hdev);
int hl_device_resume(struct hl_device *hdev);
int hl_device_reset(struct hl_device *hdev, bool hard_reset,
			bool from_hard_reset_thread);
void hl_hpriv_get(struct hl_fpriv *hpriv);
void hl_hpriv_put(struct hl_fpriv *hpriv);
int hl_device_set_frequency(struct hl_device *hdev, enum hl_pll_frequency freq);
uint32_t hl_device_utilization(struct hl_device *hdev, uint32_t period_ms);

int hl_build_hwmon_channel_info(struct hl_device *hdev,
		struct armcp_sensor *sensors_arr);

int hl_sysfs_init(struct hl_device *hdev);
void hl_sysfs_fini(struct hl_device *hdev);

int hl_hwmon_init(struct hl_device *hdev);
void hl_hwmon_fini(struct hl_device *hdev);

int hl_cb_create(struct hl_device *hdev, struct hl_cb_mgr *mgr, u32 cb_size,
		u64 *handle, int ctx_id, bool internal_cb);
int hl_cb_destroy(struct hl_device *hdev, struct hl_cb_mgr *mgr, u64 cb_handle);
int hl_cb_mmap(struct hl_fpriv *hpriv, struct vm_area_struct *vma);
struct hl_cb *hl_cb_get(struct hl_device *hdev,	struct hl_cb_mgr *mgr,
			u32 handle);
void hl_cb_put(struct hl_cb *cb);
void hl_cb_mgr_init(struct hl_cb_mgr *mgr);
void hl_cb_mgr_fini(struct hl_device *hdev, struct hl_cb_mgr *mgr);
struct hl_cb *hl_cb_kernel_create(struct hl_device *hdev, u32 cb_size,
					bool internal_cb);
int hl_cb_pool_init(struct hl_device *hdev);
int hl_cb_pool_fini(struct hl_device *hdev);

void hl_cs_rollback_all(struct hl_device *hdev);
struct hl_cs_job *hl_cs_allocate_job(struct hl_device *hdev,
		enum hl_queue_type queue_type, bool is_kernel_allocated_cb);
void hl_sob_reset_error(struct kref *ref);

void goya_set_asic_funcs(struct hl_device *hdev);
void gaudi_set_asic_funcs(struct hl_device *hdev);

int hl_vm_ctx_init(struct hl_ctx *ctx);
void hl_vm_ctx_fini(struct hl_ctx *ctx);

int hl_vm_init(struct hl_device *hdev);
void hl_vm_fini(struct hl_device *hdev);

int hl_pin_host_memory(struct hl_device *hdev, u64 addr, u64 size,
			struct hl_userptr *userptr);
void hl_unpin_host_memory(struct hl_device *hdev, struct hl_userptr *userptr);
void hl_userptr_delete_list(struct hl_device *hdev,
				struct list_head *userptr_list);
bool hl_userptr_is_pinned(struct hl_device *hdev, u64 addr, u32 size,
				struct list_head *userptr_list,
				struct hl_userptr **userptr);

int hl_mmu_init(struct hl_device *hdev);
void hl_mmu_fini(struct hl_device *hdev);
int hl_mmu_ctx_init(struct hl_ctx *ctx);
void hl_mmu_ctx_fini(struct hl_ctx *ctx);
int hl_mmu_map(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr,
		u32 page_size, bool flush_pte);
int hl_mmu_unmap(struct hl_ctx *ctx, u64 virt_addr, u32 page_size,
		bool flush_pte);
void hl_mmu_swap_out(struct hl_ctx *ctx);
void hl_mmu_swap_in(struct hl_ctx *ctx);

int hl_fw_load_fw_to_device(struct hl_device *hdev, const char *fw_name,
				void __iomem *dst);
int hl_fw_send_pci_access_msg(struct hl_device *hdev, u32 opcode);
int hl_fw_send_cpu_message(struct hl_device *hdev, u32 hw_queue_id, u32 *msg,
				u16 len, u32 timeout, long *result);
int hl_fw_unmask_irq(struct hl_device *hdev, u16 event_type);
int hl_fw_unmask_irq_arr(struct hl_device *hdev, const u32 *irq_arr,
		size_t irq_arr_size);
int hl_fw_test_cpu_queue(struct hl_device *hdev);
void *hl_fw_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size,
						dma_addr_t *dma_handle);
void hl_fw_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
					void *vaddr);
int hl_fw_send_heartbeat(struct hl_device *hdev);
int hl_fw_armcp_info_get(struct hl_device *hdev);
int hl_fw_get_eeprom_data(struct hl_device *hdev, void *data, size_t max_size);
int hl_fw_init_cpu(struct hl_device *hdev, u32 cpu_boot_status_reg,
			u32 msg_to_cpu_reg, u32 cpu_msg_status_reg,
			u32 boot_err0_reg, bool skip_bmc,
			u32 cpu_timeout, u32 boot_fit_timeout);

int hl_pci_bars_map(struct hl_device *hdev, const char * const name[3],
			bool is_wc[3]);
int hl_pci_iatu_write(struct hl_device *hdev, u32 addr, u32 data);
int hl_pci_set_dram_bar_base(struct hl_device *hdev, u8 inbound_region, u8 bar,
				u64 addr);
int hl_pci_set_inbound_region(struct hl_device *hdev, u8 region,
		struct hl_inbound_pci_region *pci_region);
int hl_pci_set_outbound_region(struct hl_device *hdev,
		struct hl_outbound_pci_region *pci_region);
int hl_pci_init(struct hl_device *hdev);
void hl_pci_fini(struct hl_device *hdev);

long hl_get_frequency(struct hl_device *hdev, u32 pll_index, bool curr);
void hl_set_frequency(struct hl_device *hdev, u32 pll_index, u64 freq);
int hl_get_temperature(struct hl_device *hdev,
		       int sensor_index, u32 attr, long *value);
int hl_set_temperature(struct hl_device *hdev,
		       int sensor_index, u32 attr, long value);
int hl_get_voltage(struct hl_device *hdev,
		   int sensor_index, u32 attr, long *value);
int hl_get_current(struct hl_device *hdev,
		   int sensor_index, u32 attr, long *value);
int hl_get_fan_speed(struct hl_device *hdev,
		     int sensor_index, u32 attr, long *value);
int hl_get_pwm_info(struct hl_device *hdev,
		    int sensor_index, u32 attr, long *value);
void hl_set_pwm_info(struct hl_device *hdev, int sensor_index, u32 attr,
			long value);
u64 hl_get_max_power(struct hl_device *hdev);
void hl_set_max_power(struct hl_device *hdev, u64 value);
int hl_set_voltage(struct hl_device *hdev,
			int sensor_index, u32 attr, long value);
int hl_set_current(struct hl_device *hdev,
			int sensor_index, u32 attr, long value);

#ifdef CONFIG_DEBUG_FS

void hl_debugfs_init(void);
void hl_debugfs_fini(void);
void hl_debugfs_add_device(struct hl_device *hdev);
void hl_debugfs_remove_device(struct hl_device *hdev);
void hl_debugfs_add_file(struct hl_fpriv *hpriv);
void hl_debugfs_remove_file(struct hl_fpriv *hpriv);
void hl_debugfs_add_cb(struct hl_cb *cb);
void hl_debugfs_remove_cb(struct hl_cb *cb);
void hl_debugfs_add_cs(struct hl_cs *cs);
void hl_debugfs_remove_cs(struct hl_cs *cs);
void hl_debugfs_add_job(struct hl_device *hdev, struct hl_cs_job *job);
void hl_debugfs_remove_job(struct hl_device *hdev, struct hl_cs_job *job);
void hl_debugfs_add_userptr(struct hl_device *hdev, struct hl_userptr *userptr);
void hl_debugfs_remove_userptr(struct hl_device *hdev,
				struct hl_userptr *userptr);
void hl_debugfs_add_ctx_mem_hash(struct hl_device *hdev, struct hl_ctx *ctx);
void hl_debugfs_remove_ctx_mem_hash(struct hl_device *hdev, struct hl_ctx *ctx);

#else

static inline void __init hl_debugfs_init(void)
{
}

static inline void hl_debugfs_fini(void)
{
}

static inline void hl_debugfs_add_device(struct hl_device *hdev)
{
}

static inline void hl_debugfs_remove_device(struct hl_device *hdev)
{
}

static inline void hl_debugfs_add_file(struct hl_fpriv *hpriv)
{
}

static inline void hl_debugfs_remove_file(struct hl_fpriv *hpriv)
{
}

static inline void hl_debugfs_add_cb(struct hl_cb *cb)
{
}

static inline void hl_debugfs_remove_cb(struct hl_cb *cb)
{
}

static inline void hl_debugfs_add_cs(struct hl_cs *cs)
{
}

static inline void hl_debugfs_remove_cs(struct hl_cs *cs)
{
}

static inline void hl_debugfs_add_job(struct hl_device *hdev,
					struct hl_cs_job *job)
{
}

static inline void hl_debugfs_remove_job(struct hl_device *hdev,
					struct hl_cs_job *job)
{
}

static inline void hl_debugfs_add_userptr(struct hl_device *hdev,
					struct hl_userptr *userptr)
{
}

static inline void hl_debugfs_remove_userptr(struct hl_device *hdev,
					struct hl_userptr *userptr)
{
}

static inline void hl_debugfs_add_ctx_mem_hash(struct hl_device *hdev,
					struct hl_ctx *ctx)
{
}

static inline void hl_debugfs_remove_ctx_mem_hash(struct hl_device *hdev,
					struct hl_ctx *ctx)
{
}

#endif

/* IOCTLs */
long hl_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);
long hl_ioctl_control(struct file *filep, unsigned int cmd, unsigned long arg);
int hl_cb_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_cs_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_cs_wait_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_mem_ioctl(struct hl_fpriv *hpriv, void *data);

#endif /* HABANALABSP_H_ */
