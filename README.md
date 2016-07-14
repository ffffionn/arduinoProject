# Arduino Burglar Alarm

As part of our 'C Programming for Microcontrollers' module, we were given the task of creating a model Burglar Alarm that observed 4 different areas with configurable options for triggering the alarm in each. Each zone keeps track of it's own settings for each kind of mode so all modes can be active with different settings configurations in each of the zones. An external library we used in the implementation is included in the repo.

### Controls: 
  - Power   (Save User Settings)
  - Mode    (Change a zone's mode)
  - Mute    (Clear User Settings)
  - -       (Read device log)
  - +       (Show stats for a zone)
  - Play    (Activate/deactivate zone)
  - Prev    (Set Date + Time)
  - Next    (Change user password)
  - EQ      (Change the settings for a zone)
  - Return  (Load User Settings)
  - Num 0   (Show zone info on serial port)
  - Num 1-4 (Show zone stats for the respective zone 1-4)
