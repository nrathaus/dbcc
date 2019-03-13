/* @brief Convert the Abstract Syntax Tree generated by mpc for the DBC file
 * into an equivalent XML file.
 * @copyright Richard James Howe (2018)
 * @license MIT *
 */
#include "2bsm.h"
#include "util.h"
#include <assert.h>
#include <time.h>

/*
Add:
	<?xml-stylesheet type="text/xsl" href="yourxsl.xsl"?>
 */

static int print_escaped(FILE *o, const char *string)
{
	assert(o);
	assert(string);
	char c;
	int r = 0;
	while((c = *(string)++)) {
		switch(c) {
		case '"':  r = fputs("&quot;", o); break;
		case '\'': r = fputs("&apos;", o); break;
		case '<':  r = fputs("&lt;",   o); break;
		case '>':  r = fputs("&gt;",   o); break;
		case '&':  r = fputs("&amp;",  o); break;
		default:
			   r = fputc(c, o);
		}
		if(r < 0)
			return -1;
	}
	return 0;
}

static int indent(FILE *o, unsigned depth)
{
	assert(o);
	while(depth--)
		if(fputc('\t', o) != '\t')
			return -1;
	return 0;
}

static int pnode(FILE *o, unsigned depth, const char *node, const char *fmt, ...)
{
	assert(o);
	assert(node);
	assert(fmt);
	va_list args;
	assert(o && node && fmt);
	errno = 0;
	if(indent(o, depth) < 0)
		goto warn;
	if(fprintf(o, "<%s>", node) < 0)
		goto warn;
	assert(fmt);
	va_start(args, fmt);
	int r = vfprintf(o, fmt, args);
	va_end(args);
	if(r < 0)
		goto warn;
	if(fprintf(o, "</%s>\n", node) < 0)
		goto warn;
	return 0;
warn:
	warning("XML node generation, problem writing to FILE* <%p>: %s", o, emsg());
	return -1;
}

static int comment(FILE *o, unsigned depth, const char *fmt, ...)
{
	assert(o);
	assert(fmt);
	va_list args;
	assert(o && fmt);
	errno = 0;
	if(indent(o, depth) < 0)
		goto warn;
	if(fputs("<!-- ", o) < 0)
		goto warn;
	assert(fmt);
	va_start(args, fmt);
	int r = vfprintf(o, fmt, args);
	va_end(args);
	if(r < 0)
		goto warn;
	if(fputs(" -->\n", o) < 0)
		goto warn;
	return 0;
warn:
	warning("XML comment generation, problem writing to FILE* <%p>: %s", o, emsg());
	return -1;
}

static int signal2bsm(signal_t *sig, FILE *o, unsigned depth)
{
  assert(sig);
  assert(o);

  char szBits[128] = { 0 };
  if (sig->bit_length > 16) {
    // We need to split it into two, because we assume a <BB> is a 16 bit element (0xXX 0x00)
    fprintf(o, "									<BE Name=\"%s (LSB) Flipper\">\n", sig->name);
    fprintf(o, "										<BB Name=\"%s (LSB) - Normal\" Bits=\"0\" Size=\"%d\" />\n", sig->name, 16);

    memset(szBits, sizeof(szBits), 0);
    for (int i = 0; i < 16; i++) {
      if (strlen(szBits) > 0) {
        strcat_s(szBits, sizeof(szBits), ",");
      }
      strcat_s(szBits, sizeof(szBits), "1");
    }
    fprintf(o, "										<BB Name=\"%s (LSB) - Flipped\" Bits=\"%s\" Size=\"%d\" />\n", sig->name, szBits, 16);
    fprintf(o, "									</BE>\n");
    fprintf(o, "									<BE Name=\"%s (MSB) Flipper\">\n", sig->name);
    fprintf(o, "										<BB Name=\"%s (MSB) - Normal\" Bits=\"0\" Size=\"%d\" />\n", sig->name, sig->bit_length - 16);

    memset(szBits, sizeof(szBits), 0);
    for (int i = 0; i < sig->bit_length - 16; i++) {
      if (strlen(szBits) > 0) {
        strcat_s(szBits, sizeof(szBits), ",");
      }
      strcat_s(szBits, sizeof(szBits), "1");
    }
    fprintf(o, "										<BB Name=\"%s (MSB) - Flipped\" MultiBits=\"%s\" Size=\"%d\" />\n", sig->name, szBits, sig->bit_length - 16);
    fprintf(o, "									</BE>\n");
  }
  else {
    memset(szBits, sizeof(szBits), 0);
    for (int i = 0; i < sig->bit_length; i++) {
      if (strlen(szBits) > 0) {
        strcat_s(szBits, sizeof(szBits), ",");
      }
      strcat_s(szBits, sizeof(szBits), "1");
    }

    fprintf(o, "									<BE Name=\"%s Flipper\">\n", sig->name);
    fprintf(o, "										<BB Name=\"%s - Normal\" Bits=\"0\" Size=\"%d\" />\n", sig->name, sig->bit_length);
    fprintf(o, "										<BB Name=\"%s - Flipped\" MultiBits=\"%s\" Size=\"%d\" />\n", sig->name, szBits, sig->bit_length);
    fprintf(o, "									</BE>\n");
  }

  return 0;
}

static int msg2bsm(can_msg_t *msg, FILE *o, unsigned depth)
{
  assert(msg);
  assert(o);
  indent(o, depth);

  unsigned last_bit = 0; // Detect gaps between signals

  unsigned int padding_size = 0; // Find how much we need to pad the data to, 8, 16, 24, or 32
  for (size_t i = 0; i < msg->signal_count; i++) {
    signal_t *sig = msg->signals[i];

    if (last_bit < sig->start_bit) {
      // We have a void, create a fake signal of UNKNOWN in the middle
      padding_size += sig->start_bit - last_bit;

      last_bit = sig->start_bit;
    }

    padding_size += sig->bit_length;
    last_bit = sig->start_bit + sig->bit_length;
  }

  if (padding_size <= 8) {
    padding_size = 8;
  }
  else if (padding_size <= 16) {
    padding_size = 16;
  }
  else if (padding_size <= 24) {
    padding_size = 24;
  }
  else if (padding_size <= 32) {
    padding_size = 32;
  }

  fprintf(o, BSM_MESSAGE_PREFIX, msg->name, msg->id, msg->id, padding_size);

  last_bit = 0;
  signal_t *multiplexor = NULL;
  for (size_t i = 0; i < msg->signal_count; i++) {
    signal_t *sig = msg->signals[i];
    if (sig->is_multiplexor) {
      if (multiplexor) {
        error("multiple multiplexor values detected (only one per CAN msg is allowed) for %s", msg->name);
        return -1;
      }
      multiplexor = sig;
      continue;
    }
    if (sig->is_multiplexed)
      continue;

    if (last_bit < sig->start_bit) {
      // We have a void, create a fake signal of UNKNOWN in the middle
      char szUNKNOWN[] = "UNKNOWN";
      char szUNITS[] = "";
      signal_t unknownsig = { 0 };
      unknownsig.name = szUNKNOWN;
      unknownsig.units = szUNITS;
      unknownsig.start_bit = last_bit;
      unknownsig.bit_length = sig->start_bit - last_bit;

      if (signal2bsm(&unknownsig, o, depth + 1) < 0)
        return -1;

      last_bit = sig->start_bit;
    }

    // Generate a Signal element
    if (signal2bsm(sig, o, depth + 1) < 0)
      return -1;

    last_bit = sig->start_bit + sig->bit_length;
  }

  /*// We don't support multiplexor for now
    if (0 && multiplexor) { 
    indent(o, depth + 1);
    fprintf(o, "<multiplexor-group>\n");
    indent(o, depth + 2);
    fprintf(o, "<multiplexor>\n");
    if (signal2xml(multiplexor, o, depth + 2) < 0)
      return -1;
    indent(o, depth + 2);
    fprintf(o, "</multiplexor>\n");

    for (size_t i = 0; i < msg->signal_count; i++) {
      signal_t *sig = msg->signals[i];
      if (!(sig->is_multiplexed))
        continue;
      indent(o, depth + 2);
      fprintf(o, "<multiplexed>\n");
      pnode(o, depth + 3, "multiplexed-on", "%u", sig->switchval);
      if (signal2bsm(sig, o, depth + 3) < 0)
        return -1;
      indent(o, depth + 2);
      fprintf(o, "</multiplexed>\n");
    }
    indent(o, depth + 2);
    fprintf(o, "</multiplexor-group>\n");
  }*/

  if (fprintf(o, BSM_MESSAGE_SUFFIX) < 0)
    return -1;
  return 0;
}

int dbc2bsm(dbc_t *dbc, FILE *output, bool use_time_stamps)
{
  assert(dbc);
  assert(output);
  /**@todo print out ECU node information, and the standard XML header */
  time_t rawtime;
  struct tm timeinfo;
  time(&rawtime);
  localtime_s(&timeinfo, &rawtime);

  comment(output, 0, "Generated by dbcc (see https://github.com/howerj/dbcc)");
  fprintf(output, BSM_PREFIX);

  if (use_time_stamps) {
    char str[26] = { 0 };
    asctime_s(str, sizeof(str) - 1, &timeinfo);
    comment(output, 0, "Generated on: %s", str);
  }

  for (int i = 0; i < dbc->message_count; i++) {
    if (msg2bsm(dbc->messages[i], output, 1) < 0) {
      return -1;
    }
  }

  fprintf(output, BSM_SUFFIX);

  return 0;
}

