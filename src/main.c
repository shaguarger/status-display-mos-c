
#include "mgos.h"

#include "mgos_i2c.h"
#include "mgos_mqtt.h"
#include "mgos_neopixel.h"

#define PIN 4
#define ORDER MGOS_NEOPIXEL_ORDER_GRB
#define NUM_PIXELS 256

struct mgos_neopixel *s_strip = NULL;

typedef struct{
  int red;
  int green;
  int blue;
} led_state_t;

static led_state_t led_state[NUM_PIXELS];

static bool displayOn = true;

enum {
  ERROR_UNKNOWN_COMMAND = -1,
  ERROR_I2C_NOT_CONFIGURED = -2,
  ERROR_I2C_READ_LIMIT_EXCEEDED = -3
};

static void sub(struct mg_connection *c, const char *fmt, ...) {
  for(int i=48;i<(NUM_PIXELS-48);i++) {
    led_state[i].red = rand() % 2;
    led_state[i].green = rand() % 2;
    led_state[i].blue = rand() % 2;
  }

  char buf[100];
  struct mg_mqtt_topic_expression te = {.topic = buf, .qos = 1};
  uint16_t sub_id = mgos_mqtt_get_packet_id();
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  mg_mqtt_subscribe(c, &te, 1, sub_id);
  LOG(LL_INFO, ("Subscribing to %s (id %u)", buf, sub_id));
}

static void pub(struct mg_connection *c, const char *fmt, ...) {
  char msg[200];
  struct json_out jmo = JSON_OUT_BUF(msg, sizeof(msg));
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = json_vprintf(&jmo, fmt, ap);
  va_end(ap);
  mg_mqtt_publish(c, mgos_sys_config_get_mqtt_pub(), mgos_mqtt_get_packet_id(),
                  MG_MQTT_QOS(1), msg, n);
  LOG(LL_INFO, ("%s -> %s", mgos_sys_config_get_mqtt_pub(), msg));
}

static void ev_handler(struct mg_connection *c, int ev, void *p,
                       void *user_data) {
  struct mg_mqtt_message *msg = (struct mg_mqtt_message *) p;

  if (ev == MG_EV_MQTT_CONNACK) {
    LOG(LL_INFO, ("CONNACK: %d", msg->connack_ret_code));
    if (mgos_sys_config_get_mqtt_sub() == NULL ||
        mgos_sys_config_get_mqtt_pub() == NULL) {
      LOG(LL_ERROR, ("Run 'mgos config-set mqtt.sub=... mqtt.pub=...'"));
    } else {
      sub(c, "%s", mgos_sys_config_get_mqtt_sub());
    }
  } else if (ev == MG_EV_MQTT_SUBACK) {
    LOG(LL_INFO, ("Subscription %u acknowledged", msg->message_id));
  } else if (ev == MG_EV_MQTT_PUBLISH) {
    struct mg_str *s = &msg->payload;
    int pixel, red, green, blue;

    //LOG(LL_INFO, ("got command: [%.*s]", (int) s->len, s->p));
    /* Our subscription is at QoS 1, we must acknowledge messages sent ot us. */
    mg_mqtt_puback(c, msg->message_id);
    int nrOfParsedValues = json_scanf(s->p, s->len, "{pin:%d,red:%d,green:%d,blue:%d}", &pixel,
                   &red, &green, &blue);
    if (nrOfParsedValues == 4) {
      if(displayOn){
        mgos_neopixel_set(s_strip, pixel, red, green, blue);
        mgos_neopixel_show(s_strip);
      }
      
      //LOG(LL_INFO, ("%3d %3d %3d %3d", pixel, red, green, blue));
      
      led_state[pixel].red = red;
      led_state[pixel].green = green;
      led_state[pixel].blue = blue;
    } else {
      //{type:state,value:OFF}
      char* type = NULL;
      char* value = NULL;
      nrOfParsedValues = json_scanf(s->p, s->len, "{type:%Q,value:%Q}", &type, &value);
      if(nrOfParsedValues == 2){
        
        displayOn = !strcmp("ON", value);
        if(displayOn){
          for(int i=0;i<NUM_PIXELS;i++) {
            mgos_neopixel_set(s_strip, i, led_state[i].red, led_state[i].green, led_state[i].blue);
            //LOG(LL_INFO, ("%3d %3d %3d %3d", i, led_state[i].red, led_state[i].green, led_state[i].blue));
          }
          mgos_neopixel_show(s_strip);
          //LOG(LL_INFO, ("%s %s - reloaded", type, value));
        }else {
          for(int i=0;i<NUM_PIXELS;i++) {
            mgos_neopixel_set(s_strip, i, 0, 0, 0);
            //LOG(LL_INFO, ("%3d %3d %3d %3d", i, led_state[i].red, led_state[i].green, led_state[i].blue));
          }
          mgos_neopixel_show(s_strip);
          //LOG(LL_INFO, ("%s %s - cleared", type, value));
        }
      } else {
      pub(c, "{error: {code: %d, message: %Q, nrOfParsedValues: %d, type: %Q, value: %Q}}", ERROR_UNKNOWN_COMMAND,
          "unknown command", nrOfParsedValues, type, value);
      }
    }
  }
  (void) user_data;
}

enum mgos_app_init_result mgos_app_init(void) {
  s_strip = mgos_neopixel_create(PIN, NUM_PIXELS, ORDER);
  mgos_mqtt_add_global_handler(ev_handler, NULL);
  return MGOS_APP_INIT_SUCCESS;
}
