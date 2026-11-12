// Session-only native configuration state. GPL-3.0-or-later.
#ifndef INCLUDED_GAME_NETWORK_MODEL3_STATE_H
#define INCLUDED_GAME_NETWORK_MODEL3_STATE_H
namespace GameNetwork {
struct Model3State {
  bool m_relayConfigured = false, m_relayBootstrap = false, m_srally2RoleConfigured = false;
  bool m_srally2MaskPresentation = false;
  unsigned m_relayBootstrapFrames = 0;
};
}
#endif
