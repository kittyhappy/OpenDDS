/*
 *
 *
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#ifndef OPENDDS_DCPS_TRANSPORT_MULTICAST_RELIABLESESSIONFACTORY_H
#define OPENDDS_DCPS_TRANSPORT_MULTICAST_RELIABLESESSIONFACTORY_H

#include "Multicast_Export.h"

#include "MulticastSessionFactory.h"

ACE_BEGIN_VERSIONED_NAMESPACE_DECL
class ACE_Reactor;
ACE_END_VERSIONED_NAMESPACE_DECL

OPENDDS_BEGIN_VERSIONED_NAMESPACE_DECL

namespace OpenDDS {
namespace DCPS {

class OpenDDS_Multicast_Export ReliableSessionFactory
  : public MulticastSessionFactory {
public:
  virtual int requires_send_buffer() const;

  virtual MulticastSession_rch create(RcHandle<ReactorTask> reactor_task,
                                      MulticastDataLink* link,
                                      MulticastPeer remote_peer);
};

} // namespace DCPS
} // namespace OpenDDS

OPENDDS_END_VERSIONED_NAMESPACE_DECL

#endif  /* DCPS_RELIABLESESSIONFACTORY_H */
