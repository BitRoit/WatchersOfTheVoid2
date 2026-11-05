// Run the real production Channel with engine/session boundaries doubled by the
// existing fixture. No alternate index or packet/application implementation.
#define main existing_channel_main
#include "channel_2179_test.cpp"
#undef main

int main() {
    using namespace test;
    using coop::net::Role;
    Adapter light{"light", ReliableKind::LightState, Ensure, IsInstance, Key, Read, Apply,
                  nullptr, nullptr, nullptr, false, true};
    actor.ready=true;
    // Both base and satellite switches use lightswitch_C/trigger_lightRoot_C.
    // Distinct cooked anchors must index even with an empty/None native Key.
    for (auto key : {L"mv_base_switch", L"mv_satellite_switch"}) {
        portableKey=key;
        for (auto native : {L"", L"None"}) {
            nativeKey=native;
            Channel host(light), client(light), observer(light);
            coop::net::Session hs,cs,os;
            cs.mode=os.mode=Role::Client;
            host.SetSession(&hs);client.SetSession(&cs);observer.SetSession(&os);
            Index(host);Index(client);Index(observer);
            CHECK(host.KeyForActor(&actor)==portableKey);
            CHECK(client.KeyForActor(&actor)==portableKey);
            for (bool want : {true,false,true,false}) {
                // A local client flip sends its indexed identity to the host.
                actor.value=want;client.OnLocalEdge(&actor);
                CHECK(cs.sent.back().kind==ReliableKind::LightState);
                coop::net::KeyedTogglePayload packet{};
                std::memcpy(&packet,cs.sent.back().data.data(),sizeof packet);
                CHECK(coop::net::StringFromWireKey(packet.key)==portableKey && (packet.action!=0)==want);
                // Host and another peer replay once; duplicate/origin echo skips.
                actor.value=!want;int count=actor.calls;host.OnReliable(packet,1);
                CHECK(actor.value==want && actor.calls==count+1);
                host.OnReliable(packet,1);CHECK(actor.calls==count+1);
                actor.value=!want;observer.OnReliable(packet,1);
                CHECK(actor.value==want && actor.calls==count+2);
                client.OnReliable(packet,1);CHECK(actor.calls==count+2);
            }
            CHECK(cs.sent.size()==4);
        }
    }
    // Native keyed fallback is unchanged; absent portable identity does not
    // invent an identity for unkeyed dynamically spawned or unresolved actors.
    portableKey=L"";nativeKey=L"persisted-switch";
    Channel keyed(light);Index(keyed);CHECK(keyed.KeyForActor(&actor)==nativeKey);
    for(auto native:{L"",L"None"}) {
        nativeKey=native;Channel missing(light);Index(missing);
        CHECK(missing.KeyForActor(&actor).empty());
    }
    // Other families retain their preexisting native-key admission boundary.
    nativeKey=L"None";portableKey=L"mv_base_switch";
    Adapter ordinary=light;ordinary.allowKeylessPortableIdentity=false;
    Channel other(ordinary);Index(other);CHECK(other.KeyForActor(&actor).empty());
    // The group's keyless identity is available, while host-only authorship is
    // retained: an indexed client's native group writer still emits no state.
    Adapter group=light;group.kind=ReliableKind::LightGroupState;
    Channel root(group,Channel::Mode::HostAuth);coop::net::Session session;
    session.mode=Role::Client;root.SetSession(&session);Index(root);
    CHECK(!root.KeyForActor(&actor).empty());root.OnLocalEdge(&actor);CHECK(session.sent.empty());
    session.mode=Role::Host;root.OnLocalEdge(&actor);CHECK(session.sent.size()==1);
    ++generation;CHECK(root.KeyForActor(&actor).empty());
    std::puts("PASS base/satellite keyless identities; client press, host/observer apply, duplicates and repeat on/off");
    std::puts("PASS native fallback, unidentifiable actor refusal, opt-in isolation, host-only groups and world retirement");
}
