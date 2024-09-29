#include "vhpi_test.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static vhpiHandleT handle_x;
static vhpiHandleT handle_y;
static vhpiHandleT handle_sos;

static void y_value_change(const vhpiCbDataT *cb_data)
{
   vhpiValueT value = {
      .format = vhpiObjTypeVal
   };
   vhpi_get_value(handle_y, &value);
   check_error();
   fail_unless(value.format == vhpiIntVal);

   vhpi_printf("y value changed to %d", value.value.intg);

   if (value.value.intg == 75) {
      vhpi_control(vhpiFinish);
      check_error();
   }
   else {
      value.value.intg++;
      vhpi_put_value(handle_x, &value, vhpiDepositPropagate);
      check_error();
   }
}

static void after_after_5ns(const vhpiCbDataT *cb_data)
{
   vhpi_printf("after_after_5ns callback!");
   fail_if(1);
}

static void next_timestep(const vhpiCbDataT *cb_data)
{
   vhpi_printf("next_timestep callback!");
   fail_if(1);
}

static void after_5ns(const vhpiCbDataT *cb_data)
{
   vhpi_printf("after_5ns callback!");

   long cycles;
   vhpiTimeT now;
   vhpi_get_time(&now, &cycles);

   fail_unless(now.low == 5000000);
   fail_unless(now.high == 0);
   fail_unless(cycles == 0);

   vhpiValueT value = {
      .format = vhpiObjTypeVal
   };
   vhpi_get_value(handle_y, &value);
   check_error();
   fail_unless(value.format == vhpiIntVal);
   vhpi_printf("value=%d", value.value.intg);
   fail_unless(value.value.intg == 6);

   value.value.intg = 70;
   vhpi_put_value(handle_x, &value, vhpiDepositPropagate);
   check_error();

   vhpiCbDataT cb_data2 = {
      .reason = vhpiCbValueChange,
      .cb_rtn = y_value_change,
      .obj    = handle_y
   };
   vhpi_register_cb(&cb_data2, 0);
   check_error();

   vhpiTimeT time_1fs = {
      .low = 1
   };

   vhpiCbDataT cb_data3 = {
      .reason = vhpiCbAfterDelay,
      .cb_rtn = after_after_5ns,
      .time   = &time_1fs
   };
   vhpiHandleT cb3 = vhpi_register_cb(&cb_data3, vhpiReturnCb);
   check_error();
   fail_if(vhpi_disable_cb(cb3));
   vhpi_release_handle(cb3);

   vhpiHandleT cb4 = vhpi_register_cb(&cb_data3, vhpiReturnCb);
   check_error();
   fail_if(vhpi_remove_cb(cb4));

   vhpiCbDataT cb_data5 = {
      .reason = vhpiCbNextTimeStep,
      .cb_rtn = next_timestep,
   };
   vhpiHandleT cb5 = vhpi_register_cb(&cb_data5, vhpiReturnCb);
   check_error();
   fail_if(vhpi_remove_cb(cb5));
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vhpi_printf("start of sim callback! user data is '%s'",
               (char *)cb_data->user_data);

   vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
   check_error();
   fail_if(root == NULL);
   vhpi_printf("root handle %p", root);

   handle_x = vhpi_handle_by_name("x", root);
   check_error();

   handle_y = vhpi_handle_by_name("y", root);
   check_error();

   vhpi_release_handle(root);

   long cycles;
   vhpiTimeT now;
   vhpi_get_time(&now, &cycles);

   fail_unless(now.low == 0);
   fail_unless(now.high == 0);
   fail_unless(cycles == 0);

   vhpiValueT value = {
      .format = vhpiObjTypeVal
   };
   vhpi_get_value(handle_x, &value);
   check_error();
   fail_unless(value.format == vhpiIntVal);
   fail_unless(value.value.intg == 0);

   value.value.intg = 5;
   vhpi_put_value(handle_x, &value, vhpiDepositPropagate);
   check_error();

   vhpiTimeT time_5ns = {
      .low = 5000000
   };

   vhpiCbDataT cb_data2 = {
      .reason = vhpiCbAfterDelay,
      .cb_rtn = after_5ns,
      .time   = &time_5ns
   };
   vhpi_register_cb(&cb_data2, 0);
   check_error();
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   vhpi_printf("end of sim callback");

   vhpiValueT value = {
      .format = vhpiObjTypeVal
   };
   vhpi_get_value(handle_y, &value);
   check_error();
   fail_unless(value.format == vhpiIntVal);
   fail_unless(value.value.intg == 75);

   vhpi_release_handle(handle_x);
   vhpi_release_handle(handle_y);
   vhpi_release_handle(handle_sos);
}

void vhpi6_startup(void)
{
   vhpi_printf("hello, world!");

   vhpiCbDataT cb_data1 = {
      .reason    = vhpiCbStartOfSimulation,
      .cb_rtn    = start_of_sim,
   };
   handle_sos = vhpi_register_cb(&cb_data1, vhpiReturnCb);
   check_error();

   vhpiCbDataT cb_data2 = {
      .reason    = vhpiCbEndOfSimulation,
      .cb_rtn    = end_of_sim
   };
   vhpi_register_cb(&cb_data2, 0);
   check_error();
}
