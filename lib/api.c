/*
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.O
 *
 */

#include <string.h>

#include "pqos.h"
#include "allocation.h"
#include "os_allocation.h"
#include "os_monitoring.h"
#include "monitoring.h"
#include "os_monitoring.h"
#include "pidapi.h"
#include "cap.h"
#include "log.h"
#include "types.h"

/**
 * Value marking monitoring group structure as "valid".
 * Group becomes "valid" after successful pqos_mon_start() or
 * pqos_mon_start_pid() call.
 */
#define GROUP_VALID_MARKER (0x00DEAD00)

/*
 * =======================================
 * Allocation Technology
 * =======================================
 */

int pqos_alloc_assoc_set(const unsigned lcore,
                         const unsigned class_id)
{
	int ret;

	_pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

	if (pqos_cap_use_msr())
		ret = hw_alloc_assoc_set(lcore, class_id);
	else
		ret = os_alloc_assoc_set(lcore, class_id);

	_pqos_api_unlock();

	return ret;
}

int pqos_alloc_assoc_get(const unsigned lcore,
                         unsigned *class_id)
{
	int ret;

	_pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

	if (pqos_cap_use_msr())
		ret = hw_alloc_assoc_get(lcore, class_id);
	else
		ret = os_alloc_assoc_get(lcore, class_id);

	_pqos_api_unlock();

	return ret;
}

int pqos_alloc_assign(const unsigned technology,
                      const unsigned *core_array,
                      const unsigned core_num,
                      unsigned *class_id)
{
	int ret;

        if (core_num == 0 || core_array == NULL || class_id == NULL ||
            technology == 0)
                return PQOS_RETVAL_PARAM;

	_pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }
        if (pqos_cap_use_msr())
                ret = hw_alloc_assign(technology, core_array,
                        core_num, class_id);
        else
                ret = os_alloc_assign(technology, core_array,
                        core_num, class_id);

	_pqos_api_unlock();

        return ret;
}

int pqos_alloc_release(const unsigned *core_array,
                       const unsigned core_num)
{
	int ret;

        if (core_num == 0 || core_array == NULL)
                return PQOS_RETVAL_PARAM;

	_pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

        if (pqos_cap_use_msr())
                ret = hw_alloc_release(core_array, core_num);
        else
                ret = os_alloc_release(core_array, core_num);

	_pqos_api_unlock();

	return ret;
}

int pqos_alloc_reset(const enum pqos_cdp_config l3_cdp_cfg)
{
	int ret;

        if (l3_cdp_cfg != PQOS_REQUIRE_CDP_ON &&
            l3_cdp_cfg != PQOS_REQUIRE_CDP_OFF &&
            l3_cdp_cfg != PQOS_REQUIRE_CDP_ANY) {
                LOG_ERROR("Unrecognized L3 CDP configuration setting %d!\n",
                          l3_cdp_cfg);
                return PQOS_RETVAL_PARAM;
        }

        _pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

	if (pqos_cap_use_msr())
                ret = hw_alloc_reset(l3_cdp_cfg);
        else
                ret = os_alloc_reset(l3_cdp_cfg);

	_pqos_api_unlock();

	return ret;
}

/*
 * =======================================
 * L3 cache allocation
 * =======================================
 */

/**
 * @brief Tests if \a bitmask is contiguous
 *
 * Zero bit mask is regarded as not contiguous.
 *
 * The function shifts out first group of contiguous 1's in the bit mask.
 * Next it checks remaining bitmask content to make a decision.
 *
 * @param bitmask bit mask to be validated for contiguity
 *
 * @return Bit mask contiguity check result
 * @retval 0 not contiguous
 * @retval 1 contiguous
 */
static int
is_contiguous(uint64_t bitmask)
{
        if (bitmask == 0)
                return 0;

        while ((bitmask & 1) == 0) /**< Shift until 1 found at position 0 */
                bitmask >>= 1;

        while ((bitmask & 1) != 0) /**< Shift until 0 found at position 0 */
                bitmask >>= 1;

        return (bitmask) ? 0 : 1;  /**< non-zero bitmask is not contiguous */
}

int pqos_l3ca_set(const unsigned socket,
                  const unsigned num_cos,
		  const struct pqos_l3ca *ca)
{
	int ret;
	unsigned i;

	if (ca == NULL || num_cos == 0)
		return PQOS_RETVAL_PARAM;

	_pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

	/**
	 * Check if class bitmasks are contiguous.
	 */
	for (i = 0; i < num_cos; i++) {
		int is_contig = 0;

		if (ca[i].cdp) {
			is_contig = is_contiguous(ca[i].u.s.data_mask) &&
				is_contiguous(ca[i].u.s.code_mask);
		} else
			is_contig = is_contiguous(ca[i].u.ways_mask);

		if (!is_contig) {
			LOG_ERROR("L3 COS%u bit mask is not contiguous!\n",
			          ca[i].class_id);
			_pqos_api_unlock();
			return PQOS_RETVAL_PARAM;
		}
	}

	if (pqos_cap_use_msr())
		ret = hw_l3ca_set(socket, num_cos, ca);
	else
		ret = os_l3ca_set(socket, num_cos, ca);

	_pqos_api_unlock();

	return ret;
}

int pqos_l3ca_get(const unsigned socket,
                  const unsigned max_num_ca,
                  unsigned *num_ca,
                  struct pqos_l3ca *ca)
{
	int ret;

	if (num_ca == NULL || ca == NULL || max_num_ca == 0)
		return PQOS_RETVAL_PARAM;

	_pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }
	if (pqos_cap_use_msr())
		ret = hw_l3ca_get(socket, max_num_ca, num_ca, ca);
	else
		ret = os_l3ca_get(socket, max_num_ca, num_ca, ca);

	_pqos_api_unlock();

	return ret;
}

/*
 * =======================================
 * L2 cache allocation
 * =======================================
 */

int pqos_l2ca_set(const unsigned l2id,
                  const unsigned num_cos,
                  const struct pqos_l2ca *ca)
{
	int ret;
	unsigned i;

	if (ca == NULL || num_cos == 0)
		return PQOS_RETVAL_PARAM;

	_pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

	/**
	 * Check if class bitmasks are contiguous
	 */
	for (i = 0; i < num_cos; i++) {
		if (!is_contiguous(ca[i].ways_mask)) {
			LOG_ERROR("L2 COS%u bit mask is not contiguous!\n",
			          ca[i].class_id);
			_pqos_api_unlock();
			return PQOS_RETVAL_PARAM;
		}
	}
	if (pqos_cap_use_msr())
		ret = hw_l2ca_set(l2id, num_cos, ca);
	else
		ret = os_l2ca_set(l2id, num_cos, ca);

	_pqos_api_unlock();

	return ret;
}

int pqos_l2ca_get(const unsigned l2id,
                  const unsigned max_num_ca,
                  unsigned *num_ca,
                  struct pqos_l2ca *ca)
{
	int ret;

	if (num_ca == NULL || ca == NULL || max_num_ca == 0)
		return PQOS_RETVAL_PARAM;

	_pqos_api_lock();

	ret = _pqos_check_init(1);
	if (ret != PQOS_RETVAL_OK) {
		_pqos_api_unlock();
		return ret;
	}

	if (pqos_cap_use_msr())
		ret = hw_l2ca_get(l2id, max_num_ca, num_ca, ca);
	else
		ret = os_l2ca_get(l2id, max_num_ca, num_ca, ca);

	_pqos_api_unlock();

	return ret;
}

/*
 * =======================================
 * Memory Bandwidth Allocation
 * =======================================
 */

int pqos_mba_set(const unsigned socket,
                 const unsigned num_cos,
                 const struct pqos_mba *requested,
                 struct pqos_mba *actual)
{
	int ret;

	_pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

	ret = hw_mba_set(socket, num_cos, requested, actual);

	_pqos_api_unlock();

	return ret;

}

int pqos_mba_get(const unsigned socket,
                 const unsigned max_num_cos,
                 unsigned *num_cos,
                 struct pqos_mba *mba_tab)
{
	int ret;

	_pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

	ret = hw_mba_get(socket, max_num_cos, num_cos, mba_tab);

	_pqos_api_unlock();

	return ret;
}

int pqos_mon_reset(void)
{
        int ret;

        _pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

        ret = hw_mon_reset();

        _pqos_api_unlock();

        return ret;
}

int pqos_mon_assoc_get(const unsigned lcore,
                       pqos_rmid_t *rmid)
{
        int ret;

        _pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

        ret = hw_mon_assoc_get(lcore, rmid);

        _pqos_api_unlock();

        return ret;
}

int pqos_mon_start(const unsigned num_cores,
                   const unsigned *cores,
                   const enum pqos_mon_event event,
                   void *context,
                   struct pqos_mon_data *group)
{
        int ret;

        if (group == NULL || cores == NULL || num_cores == 0 || event == 0)
                return PQOS_RETVAL_PARAM;

        if (group->valid == GROUP_VALID_MARKER)
                return PQOS_RETVAL_PARAM;

        /**
         * Validate event parameter
         * - only combinations of events allowed
         * - do not allow non-PQoS events to be monitored on its own
         */
        if (event & (~(PQOS_MON_EVENT_L3_OCCUP | PQOS_MON_EVENT_LMEM_BW |
                       PQOS_MON_EVENT_TMEM_BW | PQOS_MON_EVENT_RMEM_BW |
                       PQOS_PERF_EVENT_IPC | PQOS_PERF_EVENT_LLC_MISS)))
                return PQOS_RETVAL_PARAM;

        if ((event & (PQOS_MON_EVENT_L3_OCCUP | PQOS_MON_EVENT_LMEM_BW |
                      PQOS_MON_EVENT_TMEM_BW | PQOS_MON_EVENT_RMEM_BW)) == 0 &&
            (event & (PQOS_PERF_EVENT_IPC | PQOS_PERF_EVENT_LLC_MISS)) != 0)
                return PQOS_RETVAL_PARAM;

        _pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

        if (pqos_cap_use_msr())
                ret = hw_mon_start(num_cores, cores, event, context, group);
        else
                ret = os_mon_start(num_cores, cores, event, context, group);

        _pqos_api_unlock();

        return ret;
}

int pqos_mon_stop(struct pqos_mon_data *group)
{
        int ret;

        if (group == NULL)
                return PQOS_RETVAL_PARAM;

        if (group->valid != GROUP_VALID_MARKER)
                return PQOS_RETVAL_PARAM;

        _pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

        if (pqos_cap_use_msr())
                ret = hw_mon_stop(group);
        else
                ret = os_mon_stop(group);

        _pqos_api_unlock();

        return ret;
}

int pqos_mon_poll(struct pqos_mon_data **groups,
                  const unsigned num_groups)
{
        int ret;
        unsigned i;

        if (groups == NULL || num_groups == 0 || *groups == NULL)
                return PQOS_RETVAL_PARAM;

        for (i = 0; i < num_groups; i++) {
                if (groups[i] == NULL)
                        return PQOS_RETVAL_PARAM;
                if (groups[i]->valid != GROUP_VALID_MARKER)
                        return PQOS_RETVAL_PARAM;
        }

        _pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

        if (pqos_cap_use_msr())
                ret = hw_mon_poll(groups, num_groups);
        else
                ret = os_mon_poll(groups, num_groups);

        _pqos_api_unlock();

        return ret;
}

int pqos_mon_start_pid(const pid_t pid,
                       const enum pqos_mon_event event,
                       void *context,
                       struct pqos_mon_data *group)
{
        int ret;

        if (group == NULL || event == 0 || pid < 0)
                return PQOS_RETVAL_PARAM;

        if (group->valid == GROUP_VALID_MARKER)
                return PQOS_RETVAL_PARAM;

        /**
         * Validate event parameter
         * - only combinations of events allowed
         * - do not allow non-PQoS events to be monitored on its own
         */
        if (event & (~(PQOS_MON_EVENT_L3_OCCUP | PQOS_MON_EVENT_LMEM_BW |
                       PQOS_MON_EVENT_TMEM_BW | PQOS_MON_EVENT_RMEM_BW |
                       PQOS_PERF_EVENT_IPC | PQOS_PERF_EVENT_LLC_MISS)))
                return PQOS_RETVAL_PARAM;

        if ((event & (PQOS_MON_EVENT_L3_OCCUP | PQOS_MON_EVENT_LMEM_BW |
                      PQOS_MON_EVENT_TMEM_BW | PQOS_MON_EVENT_RMEM_BW)) == 0 &&
            (event & (PQOS_PERF_EVENT_IPC | PQOS_PERF_EVENT_LLC_MISS)) != 0)
                return PQOS_RETVAL_PARAM;

        _pqos_api_lock();

        ret = _pqos_check_init(1);
        if (ret != PQOS_RETVAL_OK) {
                _pqos_api_unlock();
                return ret;
        }

        memset(group, 0, sizeof(*group));
        group->event = event;
        group->pid = pid;
        group->context = context;

        if (pqos_cap_use_msr()) {
#ifdef PQOS_NO_PID_API
                UNUSED_PARAM(pid);
                UNUSED_PARAM(event);
                UNUSED_PARAM(context);
                UNUSED_PARAM(group);
                LOG_ERROR("PID monitoring API not built\n");
                return PQOS_RETVAL_ERROR;
#else
                ret = pqos_pid_start(group);
#endif /* PQOS_NO_PID_API */
        } else
                ret = os_mon_start_pid(group);

        if (ret == PQOS_RETVAL_OK)
                group->valid = GROUP_VALID_MARKER;

        _pqos_api_unlock();

        return ret;

}
