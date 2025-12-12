// manager.c
// Joypad Core - Player Management System
//
// Configurable player slot management supporting both SHIFT and FIXED modes.

#include "manager.h"
#include "core/services/profiles/profile_indicator.h"
#include "core/router/router.h"
#include <stdio.h>

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// Player array
Player_t players[MAX_PLAYERS];
int playersCount = 0;

// LED patterns for PS3/Switch controllers
const uint8_t PLAYER_LEDS[] = {
  0x00, // OFF
  0x01, // LED1  0001
  0x02, // LED2  0010
  0x04, // LED3  0100
  0x08, // LED4  1000
  0x09, // LED5  1001
  0x0A, // LED6  1010
  0x0C, // LED7  1100
  0x0D, // LED8  1101
  0x0E, // LED9  1110
  0x0F, // LED10 1111
};

// ============================================================================
// CONFIGURATION
// ============================================================================

// Current slot mode (default: SHIFT for backward compatibility)
static player_slot_mode_t current_slot_mode = PLAYER_SLOT_SHIFT;
static player_config_t current_config = {
    .slot_mode = PLAYER_SLOT_SHIFT,
    .max_slots = MAX_PLAYERS,
    .auto_assign_on_press = true,
};

// ============================================================================
// INITIALIZATION
// ============================================================================

// Initialize with default configuration (SHIFT mode)
void players_init(void)
{
  printf("[players] Initializing player management (SHIFT mode, %d slots)\n", MAX_PLAYERS);

  for (int i = 0; i < MAX_PLAYERS; i++)
  {
    players[i].dev_addr = -1;
    players[i].instance = -1;
    players[i].player_number = 0;
  }

  playersCount = 0;

  // Initialize feedback subsystem (rumble and player LED patterns)
  profile_indicator_init();
}

// Initialize with custom configuration
void players_init_with_config(const player_config_t* config)
{
  if (!config) {
    printf("[players] ERROR: NULL config, using defaults\n");
    players_init();
    return;
  }

  current_config = *config;
  current_slot_mode = config->slot_mode;

  printf("[players] Initializing player management\n");
  printf("[players]   Mode: %s\n",
      config->slot_mode == PLAYER_SLOT_SHIFT ? "SHIFT" : "FIXED");
  printf("[players]   Max slots: %d\n", config->max_slots);
  printf("[players]   Auto-assign: %s\n", config->auto_assign_on_press ? "YES" : "NO");

  // Initialize all slots
  for (int i = 0; i < MAX_PLAYERS; i++)
  {
    players[i].dev_addr = -1;
    players[i].instance = -1;
    players[i].player_number = 0;
  }

  playersCount = 0;

  // Initialize feedback subsystem (rumble and player LED patterns)
  profile_indicator_init();
}

// ============================================================================
// PLAYERS TASK (call from main loop)
// ============================================================================

void players_task(void)
{
  // Run feedback state machine (rumble and player LED patterns)
  profile_indicator_task();
}

// ============================================================================
// CONFIGURATION API
// ============================================================================

void players_set_slot_mode(player_slot_mode_t mode)
{
  current_slot_mode = mode;
  current_config.slot_mode = mode;
  printf("[players] Slot mode changed to: %s\n",
      mode == PLAYER_SLOT_SHIFT ? "SHIFT" : "FIXED");
}

player_slot_mode_t players_get_slot_mode(void)
{
  return current_slot_mode;
}

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================

// Find player by dev_addr and instance
int __not_in_flash_func(find_player_index)(int dev_addr, int instance)
{
  for(int i = 0; i < MAX_PLAYERS; i++)
  {
    // Check for occupied slot with matching dev_addr and instance
    if(players[i].dev_addr == dev_addr &&
       players[i].instance == instance &&
       players[i].dev_addr != -1)  // Not an empty slot
    {
      return i;
    }
  }
  return -1;  // Not found
}

// Add player to array
int __not_in_flash_func(add_player)(int dev_addr, int instance)
{
  int player_index = -1;

  if (current_slot_mode == PLAYER_SLOT_SHIFT) {
    // SHIFT MODE: Add to end (original behavior)
    if (playersCount >= MAX_PLAYERS) {
      printf("[players] ERROR: All %d slots full (SHIFT mode)\n", MAX_PLAYERS);
      return -1;
    }

    player_index = playersCount;
    playersCount++;

  } else {
    // FIXED MODE: Find first empty slot (dev_addr == -1)
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (players[i].dev_addr == -1) {
        player_index = i;
        break;
      }
    }

    if (player_index < 0) {
      printf("[players] ERROR: All %d slots full (FIXED mode)\n", MAX_PLAYERS);
      return -1;
    }

    // Update playersCount to highest occupied slot + 1
    if (player_index >= playersCount) {
      playersCount = player_index + 1;
    }
  }

  // Initialize the player slot
  players[player_index].dev_addr = dev_addr;
  players[player_index].instance = instance;
  players[player_index].player_number = player_index + 1;

  printf("[players] Player %d assigned (dev_addr=%d, instance=%d, mode=%s)\n",
      players[player_index].player_number, dev_addr, instance,
      current_slot_mode == PLAYER_SLOT_SHIFT ? "SHIFT" : "FIXED");

  return player_index;
}

// Remove player(s) by address
void remove_players_by_address(int dev_addr, int instance)
{
  if (current_slot_mode == PLAYER_SLOT_SHIFT) {
    // SHIFT MODE: Remove and shift remaining players up (original behavior)
    int i = 0;
    while(i < playersCount)
    {
      // -1 instance removes all instances within dev_addr
      if((players[i].dev_addr == dev_addr && instance == -1) ||
         (players[i].dev_addr == dev_addr && players[i].instance == instance))
      {
        printf("[players] Removing player %d (dev_addr=%d, instance=%d, SHIFT mode)\n",
            players[i].player_number, dev_addr, instance);

        // Shift all the players after this one up in the array
        for(int j = i; j < playersCount - 1; j++)
        {
          players[j] = players[j+1];
        }

        // Mark last slot as empty
        players[playersCount - 1].dev_addr = -1;
        players[playersCount - 1].instance = -1;
        players[playersCount - 1].player_number = 0;

        // Decrement playersCount because a player was removed
        playersCount--;
      } else {
        i++;
      }
    }

    // Update the player numbers
    for(i = 0; i < playersCount; i++)
    {
      players[i].player_number = i + 1;
    }

  } else {
    // FIXED MODE: Mark slot as empty, preserve positions
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
      // -1 instance removes all instances within dev_addr
      if((players[i].dev_addr == dev_addr && instance == -1) ||
         (players[i].dev_addr == dev_addr && players[i].instance == instance))
      {
        printf("[players] Removing player %d (dev_addr=%d, instance=%d, FIXED mode - slot stays empty)\n",
            players[i].player_number, dev_addr, instance);

        // Mark slot as empty but don't shift
        players[i].dev_addr = -1;
        players[i].instance = -1;
        players[i].player_number = 0;
      }
    }

    // In FIXED mode, playersCount stays at highest occupied slot + 1
    // Recalculate playersCount based on highest occupied slot
    int highest_occupied = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (players[i].dev_addr != -1) {
        highest_occupied = i;
      }
    }
    playersCount = highest_occupied + 1;

    printf("[players] FIXED mode: playersCount now %d (highest occupied + 1)\n", playersCount);
  }

  // If all controllers disconnected, reset router outputs to neutral
  // This prevents stuck buttons from persisting after the last controller disconnects
  if (playersCount == 0) {
    router_reset_outputs();
  }
}
