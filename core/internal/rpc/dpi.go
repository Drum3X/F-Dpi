package rpc

import (
	"context"
	"time"

	"ThroneCore/gen"
	"ThroneCore/internal/dpi"
)

// F-Dpi (Linux): the GUI's DpiCoreBackend drives the DPI engine through these (core/internal/dpi).

func (s *server) DpiStart(ctx context.Context, in *gen.DpiStartRequest) (*gen.ErrorResp, error) {
	err := dpi.Start(dpi.StartRequest{
		Args:     in.GetArgs(),
		TCPPorts: in.GetTcpPorts(),
		UDPPorts: in.GetUdpPorts(),
		Grace:    time.Duration(in.GetGraceMs()) * time.Millisecond,
	})
	if err != nil {
		return &gen.ErrorResp{Error: To(err.Error())}, nil
	}
	return &gen.ErrorResp{}, nil
}

func (s *server) DpiStop(ctx context.Context, _ *gen.EmptyReq) (*gen.DpiStatus, error) {
	return dpiStatus(dpi.Stop()), nil
}

func (s *server) DpiQuery(ctx context.Context, in *gen.DpiQueryRequest) (*gen.DpiStatus, error) {
	return dpiStatus(dpi.Query(in.GetAfterSeq(), in.GetAutohostlistVersion())), nil
}

func dpiStatus(st dpi.Status) *gen.DpiStatus {
	out := &gen.DpiStatus{
		Running:             To(st.Running),
		Exited:              To(st.Exited),
		ExitCode:            To(int32(st.ExitCode)),
		Lines:               st.Lines,
		LastSeq:             To(st.LastSeq),
		AutohostlistChanged: To(st.AutoHostlistChanged),
	}
	if st.AutoHostlistChanged {
		out.Autohostlist = To(string(st.AutoHostlist))
		out.AutohostlistVersion = To(st.AutoHostlistVersion)
	}
	return out
}
