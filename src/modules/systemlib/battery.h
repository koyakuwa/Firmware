/****************************************************************************
 *
 *   Copyright (c) 2016, 2017 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file battery.h
 *
 * Library calls for battery functionality.
 *
 * @author Julian Oes <julian@oes.ch>
 */

#pragma once

#include <controllib/blocks.hpp>
#include <controllib/block/BlockParam.hpp>
#include <uORB/topics/battery_status.h>
#include <drivers/drv_hrt.h>
#include <lib/mathlib/mathlib.h>

class Battery : public control::SuperBlock
{
public:
	/**
	 * Constructor
	 */
	Battery();

	/**
	 * Destructor
	 */
	~Battery();

	/**
	 * Reset all battery stats and report invalid/nothing.
	 */
	void reset(battery_status_s *battery_status);

	/**
	 * Get the battery cell count
	 */
	int cell_count() { return _param_n_cells.get(); }

	/**
	 * Get the empty voltage per cell
	 */
	float empty_cell_voltage() { return _param_v_empty.get(); }

	/**
	 * Get the full voltage per cell
	 */
	float full_cell_voltage() { return _param_v_full.get(); }

	/**
	 * Update current battery status message.
	 *
	 * @param voltage_v: current voltage in V
	 * @param current_a: current current in A
	 * @param connected: Battery is connected
	 * @param selected_source: This battery is on the brick that the selected source for selected_source
	 * @param priority: The brick number -1. The term priority refers to the Vn connection on the LTC4417
	 * @param throttle_normalized: throttle from 0 to 1
	 */
	void updateBatteryStatus(hrt_abstime timestamp, float voltage_v, float current_a,
                           bool connected, bool selected_source, int priority,
                           float throttle_normalized, float mot[4],
                           bool armed, battery_status_s *status);
private:
	void filterVoltage(float voltage_v);
  void MBES_EKF(hrt_abstime timestamp, float voltage_v, float current_a);
  void estCurrent(float current_a, float throttle_normalized);
  void estCurrentQ(float current_a, float throttle_normalized, float mot[4]);
	void filterCurrent(float current_a);
	void sumDischarged(hrt_abstime timestamp, float current_a);
  float SOC2OCV(float soc);
  float dSOC2OCV(float soc);
  float model(float x);
  float OCV2SOC(float ocv);
  float dmodel(float p);
	void estimateRemaining(float voltage_v, float current_a, float throttle_normalized, bool armed);
	void determineWarning(bool connected);
	void computeScale();

	control::BlockParamFloat _param_v_empty;
	control::BlockParamFloat _param_v_full;
	control::BlockParamInt _param_n_cells;
	control::BlockParamFloat _param_capacity;
	control::BlockParamFloat _param_v_load_drop;
	control::BlockParamFloat _param_r_internal;
	control::BlockParamFloat _param_low_thr;
	control::BlockParamFloat _param_crit_thr;
	control::BlockParamFloat _param_emergency_thr;

	float _voltage_filtered_v;
  float _current_est;
	float _current_filtered_a;
	float _discharged_mah;
	float _remaining_voltage;		///< normalized battery charge level remaining based on voltage
	float _remaining_capacity;		///< normalized battery charge level remaining based on capacity
	float _remaining;			///< normalized battery charge level, selected based on config param
  float _remaining_error;
	float _scale;
	uint8_t _warning;
	hrt_abstime _last_timestamp;

  // MBES variables
  float _mbes_u;
  float _mbes_um;
  float _mbes_y;
  matrix::Matrix<float, 4, 1> _mbes_xhat;
  matrix::Matrix<float,4, 1> _mbes_xhatm;
  matrix::Matrix<float,1, 1> _mbes_yhat;
  matrix::Matrix<float,1, 1> _mbes_yhatm;
  float _mbes_th[4];
  float _mbes_Ts;
  float _mbes_SOC0, _mbes_FCC, _mbes_R0, _mbes_Rd, _mbes_Cd;
  float _mbes_aR0, _mbes_aRd, _mbes_aCd;
  matrix::Matrix<float,4, 4> _mbes_Q;
  matrix::Matrix<float,4, 4> _mbes_P;
  float _mbes_R;
  matrix::Matrix<float,4, 1> _mbes_G;
  matrix::Matrix<float,4, 4> _mbes_Pm;
  matrix::Matrix<float,1, 4> _mbes_C;
  matrix::Matrix<float,4, 4> _mbes_A;
  matrix::Matrix<float,4, 4> _mbes_A0;
  matrix::Matrix<float,4, 1> _mbes_B0;
  matrix::Matrix<float,4, 4> _mbes_Ad;
  float _mbes_Rl[4], _mbes_Cl[4];
  matrix::Matrix<float,1, 4> _mbes_tomato;
  matrix::Matrix<float,4, 4> _mbes_cheeze;
  matrix::Matrix<float,1, 1> _mbes_pasta;
  matrix::Matrix<float,1, 1> _mbes_basil;
  int _mbes_initial_set = 0;
  int _mbes_flag;
  float _mbes_dt;
  hrt_abstime _mbes_tm;
};
