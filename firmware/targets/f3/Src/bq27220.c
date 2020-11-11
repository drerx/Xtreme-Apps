#include <bq27220.h>
#include <bq27220_reg.h>
#include <i2c.h>
#include <stdbool.h>

uint16_t bq27220_read_word(uint8_t address) {
    uint8_t data[2] = { address };
    if (HAL_I2C_Master_Transmit(&POWER_I2C, BQ27220_ADDRESS, data, 1, 2000) != HAL_OK) {
        return 0;
    }

    if (HAL_I2C_Master_Receive(&POWER_I2C, BQ27220_ADDRESS, data, 2, 2000) != HAL_OK) {
        return 0;
    }
    return *(uint16_t*)data;
}

bool bq27220_control(uint16_t control) {
    uint8_t data[3] = { CommandControl };
    data[1] = (control>>8) & 0xFF;
    data[2] = control & 0xFF;
    if (HAL_I2C_Master_Transmit(&POWER_I2C, BQ27220_ADDRESS, data, 3, 2000) != HAL_OK) {
        return false;
    }

    return true;
}

void bq27220_init() {
    bq27220_control(Control_ENTER_CFG_UPDATE);
    bq27220_control(Control_SET_PROFILE_2);
    bq27220_control(Control_EXIT_CFG_UPDATE);
}

uint16_t bq27220_get_voltage() {
    return bq27220_read_word(CommandVoltage);
}

int16_t bq27220_get_current() {
    return (int16_t)bq27220_read_word(CommandCurrent);
}

uint16_t bq27220_get_full_charge_capacity() {
    return bq27220_read_word(CommandFullChargeCapacity);
}

uint16_t bq27220_get_remaining_capacity() {
    return bq27220_read_word(CommandRemainingCapacity);
}

uint16_t bq27220_get_state_of_charge() {
    return bq27220_read_word(CommandStateOfCharge);
}
