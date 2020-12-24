/*
 * Copyright 2020 Rubicon Communications, LLC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/socket.h>
#include <linux/if.h>

#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vpp/app/version.h>
#include <vnet/format_fns.h>

#include <linux-cp/lcp_interface.h>
#include <linux-cp/lcp.api_enum.h>
#include <linux-cp/lcp.api_types.h>

static u16 lcp_msg_id_base;
#define REPLY_MSG_ID_BASE lcp_msg_id_base
#include <vlibapi/api_helper_macros.h>

static lip_host_type_t
api_decode_host_type (vl_api_lcp_itf_host_type_t type)
{
  if (type == LCP_API_ITF_HOST_TUN)
    return LCP_ITF_HOST_TUN;

  return LCP_ITF_HOST_TAP;
}

static vl_api_lcp_itf_host_type_t
api_encode_host_type (lip_host_type_t type)
{
  if (type == LCP_ITF_HOST_TUN)
    return LCP_API_ITF_HOST_TUN;

  return LCP_API_ITF_HOST_TAP;
}

void
lcp_set_auto_intf (u8 is_auto)
{
  lcp_main_t *lcpm = &lcp_main;

  lcpm->auto_intf = (is_auto != 0);
}

int
lcp_auto_intf (void)
{
  lcp_main_t *lcpm = &lcp_main;

  return lcpm->auto_intf;
}

static void
vl_api_lcp_itf_pair_add_del_t_handler (vl_api_lcp_itf_pair_add_del_t *mp)
{
  u32 phy_sw_if_index;
  vl_api_lcp_itf_pair_add_del_reply_t *rmp;
  lip_host_type_t lip_host_type;
  int rv;

  if (!vnet_sw_if_index_is_api_valid (mp->sw_if_index))
    {
      rv = VNET_API_ERROR_INVALID_SW_IF_INDEX;
      goto bad_sw_if_index;
    }

  phy_sw_if_index = mp->sw_if_index;
  lip_host_type = api_decode_host_type (mp->host_if_type);
  if (mp->is_add)
    {
      u8 *host_if_name, *netns;
      int host_len, netns_len;

      host_if_name = netns = 0;

      /* lcp_itf_pair_create expects vec of u8 */
      host_len = clib_strnlen ((char *) mp->host_if_name,
			       sizeof (mp->host_if_name) - 1);
      vec_add (host_if_name, mp->host_if_name, host_len);
      vec_add1 (host_if_name, 0);

      netns_len =
	clib_strnlen ((char *) mp->namespace, sizeof (mp->namespace) - 1);
      vec_add (netns, mp->namespace, netns_len);
      vec_add1 (netns, 0);

      rv = lcp_itf_pair_create (phy_sw_if_index, host_if_name, lip_host_type,
				netns);

      vec_free (host_if_name);
      vec_free (netns);
    }
  else
    {
      rv = lcp_itf_pair_delete (phy_sw_if_index);
    }

  BAD_SW_IF_INDEX_LABEL;
  REPLY_MACRO (VL_API_LCP_ITF_PAIR_ADD_DEL_REPLY);
}

static void
send_lcp_itf_pair_details (index_t lipi, vl_api_registration_t *rp,
			   u32 context)
{
  vl_api_lcp_itf_pair_details_t *rmp;
  lcp_itf_pair_t *lcp_pair = lcp_itf_pair_get (lipi);

  REPLY_MACRO_DETAILS4 (
    VL_API_LCP_ITF_PAIR_DETAILS, rp, context, ({
      rmp->phy_sw_if_index = lcp_pair->lip_phy_sw_if_index;
      rmp->host_sw_if_index = lcp_pair->lip_host_sw_if_index;
      rmp->vif_index = lcp_pair->lip_vif_index;
      rmp->host_if_type = api_encode_host_type (lcp_pair->lip_host_type);

      clib_strncpy ((char *) rmp->host_if_name,
		    (char *) lcp_pair->lip_host_name,
		    vec_len (lcp_pair->lip_host_name) - 1);

      clib_strncpy ((char *) rmp->namespace, (char *) lcp_pair->lip_namespace,
		    vec_len (lcp_pair->lip_namespace));
    }));
}

static void
vl_api_lcp_itf_pair_get_t_handler (vl_api_lcp_itf_pair_get_t *mp)
{
  vl_api_lcp_itf_pair_get_reply_t *rmp;
  i32 rv = 0;

  REPLY_AND_DETAILS_MACRO (
    VL_API_LCP_ITF_PAIR_GET_REPLY, lcp_itf_pair_pool,
    ({ send_lcp_itf_pair_details (cursor, rp, mp->context); }));
}

static void
vl_api_lcp_default_ns_set_t_handler (vl_api_lcp_default_ns_set_t *mp)
{
  vl_api_lcp_default_ns_set_reply_t *rmp;
  int rv;

  mp->namespace[LCP_NS_LEN - 1] = 0;
  rv = lcp_set_default_ns (mp->namespace);

  REPLY_MACRO (VL_API_LCP_DEFAULT_NS_SET_REPLY);
}

static void
vl_api_lcp_default_ns_get_t_handler (vl_api_lcp_default_ns_get_t *mp)
{
  lcp_main_t *lcpm = &lcp_main;
  vl_api_lcp_default_ns_get_reply_t *rmp;
  vl_api_registration_t *reg;
  char *ns;

  reg = vl_api_client_index_to_registration (mp->client_index);
  if (!reg)
    return;

  rmp = vl_msg_api_alloc (sizeof (*rmp));
  clib_memset (rmp, 0, sizeof (*rmp));
  rmp->_vl_msg_id = (VL_API_LCP_DEFAULT_NS_GET_REPLY + lcpm->msg_id_base);
  rmp->context = mp->context;

  ns = (char *) lcp_get_default_ns ();
  if (ns)
    clib_strncpy ((char *) rmp->namespace, ns, LCP_NS_LEN - 1);

  vl_api_send_msg (reg, (u8 *) rmp);
}

static void
vl_api_lcp_itf_pair_replace_begin_t_handler (
  vl_api_lcp_itf_pair_replace_begin_t *mp)
{
  vl_api_lcp_itf_pair_replace_begin_reply_t *rmp;
  int rv;

  rv = lcp_itf_pair_replace_begin ();

  REPLY_MACRO (VL_API_LCP_ITF_PAIR_REPLACE_BEGIN_REPLY);
}

static void
vl_api_lcp_itf_pair_replace_end_t_handler (
  vl_api_lcp_itf_pair_replace_end_t *mp)
{
  vl_api_lcp_itf_pair_replace_end_reply_t *rmp;
  int rv = 0;

  rv = lcp_itf_pair_replace_end ();

  REPLY_MACRO (VL_API_LCP_ITF_PAIR_REPLACE_END_REPLY);
}

/*
 * Set up the API message handling tables
 */
#include <linux-cp/lcp.api.c>

static clib_error_t *
lcp_plugin_api_hookup (vlib_main_t *vm)
{
  /* Ask for a correctly-sized block of API message decode slots */
  lcp_msg_id_base = setup_message_id_table ();

  return (NULL);
}

VLIB_INIT_FUNCTION (lcp_plugin_api_hookup);

#include <vpp/app/version.h>
VLIB_PLUGIN_REGISTER () = {
  .version = VPP_BUILD_VER,
  .description = "Linux Control Plane - Interface Mirror",
  .default_disabled = 1,
};

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
