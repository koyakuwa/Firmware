/****************************************************************************
 *
 *   Copyright (c) 2016 PX4 Development Team. All rights reserved.
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
 * @file battery.cpp
 *
 * Library calls for battery functionality.
 *
 * @author Julian Oes <julian@oes.ch>
 */

#include "battery.h"

Battery::Battery() :
	SuperBlock(nullptr, "BAT"),
	_param_v_empty(this, "V_EMPTY"),
	_param_v_full(this, "V_CHARGED"),
	_param_n_cells(this, "N_CELLS"),
	_param_capacity(this, "CAPACITY"),
	_param_v_load_drop(this, "V_LOAD_DROP"),
	_param_r_internal(this, "R_INTERNAL"),
	_param_low_thr(this, "LOW_THR"),
	_param_crit_thr(this, "CRIT_THR"),
	_param_emergency_thr(this, "EMERGEN_THR"),
	_voltage_filtered_v(-1.0f),
  _current_est(-1.0f),
	_current_filtered_a(-1.0f),
	_discharged_mah(0.0f),
	_remaining_voltage(1.0f),
	_remaining_capacity(1.0f),
	_remaining(1.0f),
  _remaining_error(0.0f),
	_scale(1.0f),
	_warning(battery_status_s::BATTERY_WARNING_NONE),
	_last_timestamp(0)
{
	/* load initial params */
	updateParams();
}

Battery::~Battery()
{
}

void
Battery::reset(battery_status_s *battery_status)
{
	memset(battery_status, 0, sizeof(*battery_status));
	battery_status->current_a = -1.0f;
	battery_status->remaining = 1.0f;
  battery_status->remaining_error = 0.0f;
	battery_status->scale = 1.0f;
	battery_status->cell_count = _param_n_cells.get();
	// TODO: check if it is sane to reset warning to NONE
	battery_status->warning = battery_status_s::BATTERY_WARNING_NONE;
	battery_status->connected = false;
}

void
Battery::updateBatteryStatus(hrt_abstime timestamp, float voltage_v, float current_a,
                             bool connected, bool selected_source, int priority,
                             float throttle_normalized, float motor_out[4],
                             bool armed, battery_status_s *battery_status)
{
	reset(battery_status);
	battery_status->timestamp = timestamp;
	filterVoltage(voltage_v);
  // estCurrent(current_a, throttle_normalized);
  estCurrentQ(current_a, throttle_normalized, motor_out);
	filterCurrent(current_a);
	sumDischarged(timestamp, current_a);
  MBES_EKF(timestamp, voltage_v, current_a);
	estimateRemaining(voltage_v, current_a, throttle_normalized, armed);
	determineWarning(connected);
	computeScale();

	if (_voltage_filtered_v > 2.1f) {
		battery_status->voltage_v = voltage_v;
		battery_status->voltage_filtered_v = _voltage_filtered_v;
		battery_status->scale = _scale;
		// battery_status->current_a = current_a;
    battery_status->current_a = _current_est;
		battery_status->current_filtered_a = _current_filtered_a;
		battery_status->discharged_mah = _discharged_mah;
		battery_status->warning = _warning;
		battery_status->remaining = _remaining;
    battery_status->remaining_error = _remaining_error;
		battery_status->connected = connected;
		battery_status->system_source = selected_source;
		battery_status->priority = priority;
	}
}

void
Battery::filterVoltage(float voltage_v)
{
	if (_voltage_filtered_v < 0.0f) {
		_voltage_filtered_v = voltage_v;
	}

	// TODO: inspect that filter performance
	const float filtered_next = _voltage_filtered_v * 0.99f + voltage_v * 0.01f;

	if (PX4_ISFINITE(filtered_next)) {
		_voltage_filtered_v = filtered_next;
	}
}

void
Battery::MBES_EKF(hrt_abstime timestamp, float voltage_v, float current_a)
{
  if (_current_est < 0.0f) {
		_mbes_tm = 0;
		return;
	}
	if (_mbes_tm != 0) {
		_mbes_dt += ((float)(timestamp - _mbes_tm)) / 1e6f;
	}
	_mbes_tm = timestamp;
  if (_mbes_dt > 0.5f){
    _mbes_flag = 1;
    _mbes_dt = 0.0f;
  }
  _mbes_u = -1.0f * _current_est;
  _mbes_y = voltage_v/_param_n_cells.get();
  if (_mbes_initial_set == 0 ) {
    _mbes_SOC0 = OCV2SOC(voltage_v/_param_n_cells.get());
    _mbes_FCC = 2700.0f * 3.6f;
    // Bat0
    // _mbes_aR0 = -7.64525398282730;
    // _mbes_aRd = -2.94719087016744;
    // _mbes_aCd = 6.53765269854994;
    // Bat1
    // _mbes_aR0 = -8.5686f;
    // _mbes_aRd = -3.4914f;
    // _mbes_aCd = 6.9440;
    // Bat2
    // _mbes_aR0 = -8.5804f;
    // _mbes_aRd = -3.7237f;
    // _mbes_aCd = 6.880f;
    // Bat3
    // _mbes_aR0 = -8.6085f;
    // _mbes_aRd = -3.8372f;
    // _mbes_aCd = 6.9238f;
    // Bat1x
    _mbes_aR0 = -8.5457f;
    _mbes_aRd = -3.3752f;
    _mbes_aCd = 6.1933f;
    // Bat2x
    // _mbes_aR0 = -8.5566f;
    // _mbes_aRd = -3.7631f;
    // _mbes_aCd = 7.1208f;
    // Bat3x
    // _mbes_aR0 = -8.5879f;
    // _mbes_aRd = -3.8373f;
    // _mbes_aCd = 6.9985f;

    _mbes_R0 = exp(_mbes_aR0);
    _mbes_Rd = exp(_mbes_aRd);
    _mbes_Cd = exp(_mbes_aCd);
    _mbes_Ts = 0.5f;
    _mbes_th[0] = _mbes_R0;
    _mbes_th[1] = _mbes_Rd;
    _mbes_th[2] = _mbes_Cd;
    _mbes_th[3] = _mbes_FCC;
    _mbes_A(0,0) = 0.0f;
    _mbes_A0(0,0) = 1.0f;
    _mbes_B0(0,0) = 100.0f*_mbes_Ts/_mbes_th[3];
    _mbes_C(0,0) = 0.0f;
    for (int i = 1; i < 4; i++) {
      _mbes_Rl[i] = 8*_mbes_th[1]/(2*i-1)/(2*i-1)/M_PI_F/M_PI_F;
      _mbes_Cl[i] = _mbes_th[2]/2;
      _mbes_A(i,i) = -1.0f / ( _mbes_Rl[i]*_mbes_Cl[i] );
      _mbes_A0(i,i) = 1.0f + _mbes_Ts * _mbes_A(i,i);
      _mbes_B0(i,0) = _mbes_Ts/_mbes_Cl[i];
      _mbes_C(0,i) = 1.0f;
    }
    _mbes_Ad = (_mbes_A*_mbes_Ts);
    // initial setting of kalman fileter
    _mbes_Q(0,0) = 0.01f * _mbes_Ts * _mbes_Ts;
    for (int i = 1; i < 4; i++) {
      _mbes_Q(i,i) = 1.0e-6f;
    }
    for (int i = 0; i < 4; i++) {
      _mbes_Ad(i,i) = exp(_mbes_Ad(i,i));
    }
    _mbes_R=0.075f;
    _mbes_xhat(0,0) = _mbes_SOC0;
    _mbes_P(0,0) = 1.0e2f;
    _mbes_P(1,1) = 1.0e-4f;
    _mbes_P(2,2) = 1.0e-4f;
    _mbes_P(3,3) = 1.0e-4f;
    _mbes_initial_set = 1;
    _mbes_tomato = matrix::ones<float, 1, 4>();
    _mbes_pasta = matrix::ones<float, 1, 1>();
    _mbes_cheeze = matrix::eye<float, 4>();
    _mbes_tomato(0,0) = 0.0f;
  } else if (_mbes_flag == 1){
    _mbes_flag = 0;
    _mbes_xhatm = (_mbes_A0*_mbes_xhat) + (_mbes_B0*_mbes_um);
    _mbes_yhatm =  (SOC2OCV(_mbes_xhatm(0,0)) * _mbes_pasta ) + (_mbes_tomato*_mbes_xhatm) + ((_mbes_th[0]*_mbes_u)*_mbes_pasta);
    _mbes_Pm = _mbes_Ad * _mbes_P * _mbes_Ad.T() + _mbes_Q;
    _mbes_C(0,0) = dSOC2OCV(_mbes_xhatm(0,0));
    _mbes_basil = _mbes_C * _mbes_Pm * _mbes_C.T();
    _mbes_G = ( _mbes_Pm * _mbes_C.T() ) / (  _mbes_basil(0,0) + _mbes_R );
    _mbes_xhat = _mbes_xhatm + ( _mbes_G * ( ( _mbes_y *_mbes_pasta ) - _mbes_yhatm) );
    _mbes_P = ( _mbes_cheeze - (_mbes_G*_mbes_C) ) * _mbes_Pm;
    // _mbes_xhat = _mbes_xhat + (_mbes_u * _mbes_Ts / _mbes_FCC * 100.0f);
  }
  _mbes_um = _mbes_u;
}


float Battery::SOC2OCV(float soc){
    // soc2OCV soc-OCV特性モデル
    // soc-OCV特性の簡易モデル
    // 係数の設定
    float ocv;
    // soc →0%で計算式が発散するので
    // soc < 2% およびsoc>98%では線形外挿に切り替える
    if (soc > 98.0f) {
        ocv = dSOC2OCV(soc)*(soc-98.0f) + model(98.0f);
    }
    else if (soc < 2.0f){
        ocv = dSOC2OCV(soc)*(soc-2.0f) + model(2.0f);
    }
    else{
        ocv = model(soc);
    }
    return ocv;
}

float Battery::dSOC2OCV(float soc){
    // soc2OCV soc-OCV特性モデル
    // soc-OCV特性の簡易モデル
    // 係数の設定
    float docv;
    // soc →0%で計算式が発散するので
    // soc < 2% およびsoc>98%では線形外挿に切り替える
    if (soc > 98.0f) {
        docv = dmodel(98.0f);
    }
    else if (soc < 2.0f){
        docv = dmodel(2.0f);
    }
    else{
        docv = dmodel(soc);
    }
    return docv;
}

float Battery::model(float x){
    float ans;
    float K1 = -0.9267, K2 = -0.0146, K3 = 0.1400, K4 = -1.6944;
    float E0 = 2.5632;
    float xx = x / 100.0f;
    ans = E0 + ( K1 * logf(xx) ) + ( K2 * logf(1.0f-(xx)) ) - ( K3/xx ) - ( K4 * xx );
    return ans;
}


float Battery::OCV2SOC(float ocv)
{
    // newton method
    float eps = 1.0e-2;
    int max = 1000;
    int i;
    float a,newa;
    float soc_temp = 50.0f;
    //a = 100 * _remaining_voltage;
    a = 90.0f;
    for (i = 1; i < max; i++){
      newa=a-((SOC2OCV(a)-ocv)/dSOC2OCV(a));
      if(fabsf(newa-a)<eps)
        break;
      a=newa;
      if(i==max-1) {
        newa = soc_temp;
        break;
      }
    }
    // if (newa > 100.0f){ newa = 100.0f;}
    soc_temp = newa;
    // printf("解の値は %e\n最小値は %e\n収束するのに %d 回かかりました。\n",newa,f(newa),count);
    return newa;
}

float Battery::dmodel(float p)
{
    float ans;
    float K1 = -0.9267f, K2 = -0.0146f, K3 = 0.1400f, K4 = -1.6944f;
    float pp = p /100.0f;
    ans = ( K1/pp ) - ( K2/(1.0f-pp) ) + ( K3 / (pp*pp) ) - K4;
    ans = ans / 100.0f;
    return ans;
}


void
Battery::estCurrent(float current_a, float throttle_normalized)
{
  if (throttle_normalized > 0.0f ){
    // float wp_a = 9.78696f;
    // float wp_b = -5.22771f;
    // float wp_c = 2.66362f;
    // float wp_d = 0.164048f;
    // float wp_a = 1.91152e-08f;
    // float wp_b = -5.40447e-05f;
    // float wp_c = 0.0530999f;
    // float wp_d = -17.5142f;
    // // A
    // float wp_a = 5.36591e-08;
    // float wp_b = -0.000184465;
    // float wp_c  = 0.214276;
    // float wp_d = -82.6547;
    // // B
    // float wp_a  = 4.19723e-09;
    // float wp_b = -7.44473e-06;
    // float wp_c = 0.00339255;
    // float wp_d = 0.979811;
    // C
    // float wp_a = -3.11084e-009;
    // float wp_b = 1.60724e-005;
    // float wp_c = -0.0207343;
    // float wp_d = 8.40601;
    // Cx
    float wp_a = -6.22186e-09;
    float wp_b = 3.2145e-05;
    float wp_c = -0.0414685;
    float wp_d  = 16.8119;
    float x = 800.0f * throttle_normalized + 800.0f;
    _current_est = wp_a*x*x*x + wp_b*x*x + wp_c*x +  wp_d;
  } else {
    _current_est = 0.0f;
  }
  _current_est = 4.0f * _current_est;
}

void
Battery::estCurrentQ(float current_a, float throttle_normalized, float mot[4])
{
  // // C
  // float wp_a = -3.11084e-009;
  // float wp_b = 1.60724e-005;
  // float wp_c = -0.0207343;
  // float wp_d = 8.40601;
  // Cx
  float wp_a = -6.22186e-09;
  float wp_b = 3.2145e-05;
  float wp_c = -0.0414685;
  float wp_d  = 16.8119;
  float x[4], y[4];
  _current_est = 0.0f;
  for (int i = 0; i<4; i++){
    x[i] = 800.0f * mot[i] + 800.0f;
    if (x[i] > 900){
      y[i] = wp_a*x[i]*x[i]*x[i] + wp_b*x[i]*x[i] + wp_c*x[i] +  wp_d;
    } else {
      y[i] = 0.34;
    }
    _current_est = _current_est + y[i];
  }
}

void
Battery::filterCurrent(float current_a)
{
	if (_current_filtered_a < 0.0f) {
		_current_filtered_a = _current_est;
	}

	// ADC poll is at 100Hz, this will perform a low pass over approx 500ms
	const float filtered_next = _current_filtered_a * 0.98f + _current_est * 0.02f;

	if (PX4_ISFINITE(filtered_next)) {
		_current_filtered_a = filtered_next;
	}
  // _current_filtered_a = _mbes_P(0,0);
}


void
Battery::sumDischarged(hrt_abstime timestamp, float current_a)
{
	// Not a valid measurement
	if (_current_est < 0.0f) {
		// Because the measurement was invalid we need to stop integration
		// and re-initialize with the next valid measurement
		_last_timestamp = 0;
		return;
	}

	// Ignore first update because we don't know dT.
	if (_last_timestamp != 0) {
		_discharged_mah += _current_est * ((float)(timestamp - _last_timestamp)) / 1e3f / 3600.0f;
	}
	_last_timestamp = timestamp;
}

void
Battery::estimateRemaining(float voltage_v, float current_a, float throttle_normalized, bool armed)
{
	const float bat_r = _param_r_internal.get();

	// remaining charge estimate based on voltage and internal resistance (drop under load)
	float bat_v_empty_dynamic = _param_v_empty.get();

	if (bat_r >= 0.0f) {
		bat_v_empty_dynamic -= current_a * bat_r;

	} else {
		// assume 10% voltage drop of the full drop range with motors idle
		const float thr = (armed) ? ((fabsf(throttle_normalized) + 0.1f) / 1.1f) : 0.0f;

		bat_v_empty_dynamic -= _param_v_load_drop.get() * thr;
	}

	// the range from full to empty is the same for batteries under load and without load,
	// since the voltage drop applies to both the full and empty state
	const float voltage_range = (_param_v_full.get() - _param_v_empty.get());

	// remaining battery capacity based on voltage
	const float rvoltage = (voltage_v - (_param_n_cells.get() * bat_v_empty_dynamic))
			       / (_param_n_cells.get() * voltage_range);
	const float rvoltage_filt = _remaining_voltage * 0.99f + rvoltage * 0.01f;

	if (PX4_ISFINITE(rvoltage_filt)) {
		_remaining_voltage = rvoltage_filt;
	}

	// remaining battery capacity based on used current integrated time
  if (!armed) {
    _discharged_mah =  _param_capacity.get() * (100.0f - OCV2SOC(voltage_v/_param_n_cells.get()) ) / 100.0f;
  }
	const float rcap = 1.0f - _discharged_mah / _param_capacity.get();
	const float rcap_filt = _remaining_capacity * 0.99f + rcap * 0.01f;

	if (PX4_ISFINITE(rcap_filt)) {
		_remaining_capacity = rcap_filt;
	}

	// limit to sane values
	_remaining_voltage = (_remaining_voltage < 0.0f) ? 0.0f : _remaining_voltage;
	_remaining_voltage = (_remaining_voltage > 1.0f) ? 1.0f : _remaining_voltage;

	_remaining_capacity = (_remaining_capacity < 0.0f) ? 0.0f : _remaining_capacity;
	_remaining_capacity = (_remaining_capacity > 1.0f) ? 1.0f : _remaining_capacity;

	// choose which quantity we're using for final reporting
	if (_param_capacity.get() > 0.0f) {
		// if battery capacity is known, use discharged current for estimate,
		// but don't show more than voltage estimate
		// _remaining = fminf(_remaining_voltage, _remaining_capacity);
    _remaining = _remaining_capacity;
    // _remaining = _mbes_xhat(0,0)*0.01f;
    // _remaining_error = _mbes_P(0,0);

	} else {
		// else use voltage
		_remaining = _remaining_voltage;
	}
}

void
Battery::determineWarning(bool connected)
{
	if (connected) {
		// propagate warning state only if the state is higher, otherwise remain in current warning state
		if (_remaining < _param_emergency_thr.get() || (_warning == battery_status_s::BATTERY_WARNING_EMERGENCY)) {
			_warning = battery_status_s::BATTERY_WARNING_EMERGENCY;

		} else if (_remaining < _param_crit_thr.get() || (_warning == battery_status_s::BATTERY_WARNING_CRITICAL)) {
			_warning = battery_status_s::BATTERY_WARNING_CRITICAL;

		} else if (_remaining < _param_low_thr.get() || (_warning == battery_status_s::BATTERY_WARNING_LOW)) {
			_warning = battery_status_s::BATTERY_WARNING_LOW;
		}
	}
}

void
Battery::computeScale()
{
	const float voltage_range = (_param_v_full.get() - _param_v_empty.get());

	// reusing capacity calculation to get single cell voltage before drop
	const float bat_v = _param_v_empty.get() + (voltage_range * _remaining_voltage);

	_scale = _param_v_full.get() / bat_v;

	if (_scale > 1.3f) { // Allow at most 30% compensation
		_scale = 1.3f;

	} else if (!PX4_ISFINITE(_scale) || _scale < 1.0f) { // Shouldn't ever be more than the power at full battery
		_scale = 1.0f;
	}
}
