// Structs need to be in external header files for Arduino
// http://stackoverflow.com/questions/15415839/how-to-call-function-and-retrieve-two-returns-in-arduino-or-c


struct HsbColor {
  int hue;
  int sat;
  int bri;
};

// Is this ever needed? So far it is not being used
struct xy {
  float x;
  float y;
};


