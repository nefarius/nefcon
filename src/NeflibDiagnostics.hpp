#pragma once

//
// Adapter that forwards neflib's optional, framework-agnostic diagnostics callback
// (nefarius::utilities::DiagnosticEvent, see <nefarius/neflib/Diagnostics.hpp>) onto nefcon's
// EasyLogging++ "default" logger, so intermediate detail from multi-step neflib operations
// (restart strategy attempts, INF install dialog interception, service-deletion retries, ...)
// becomes visible the same way every other nefcon log line does.
//
// Gating: DiagnosticLevel::Verbose events are only ever emitted through logger->verbose(1, ...),
// i.e. they are invisible unless the caller passed --verbose - matching every other verbose-only
// nefcon log line. Info/Warning/Error events are always visible, same as nefcon's own info/warn/
// error calls elsewhere. neflib itself only ever emits Warning/Error for genuine library-internal
// anomalies (e.g. a worker thread throwing); everything that duplicates a final outcome nefcon
// already reports explicitly (e.g. RestartDeviceInstance's overall success/failure) is emitted by
// neflib at Verbose, specifically to avoid double-logging the same line twice at normal verbosity.
//

namespace nefarius::nefcon
{
	inline void RegisterNeflibDiagnosticsAdapter()
	{
		using nefarius::utilities::DiagnosticEvent;
		using nefarius::utilities::DiagnosticLevel;

		nefarius::utilities::SetDiagnosticCallback([](const DiagnosticEvent& event)
		{
			el::Logger* logger = el::Loggers::getLogger("default");

			std::string line(event.Operation);

			if (!event.Subject.empty())
			{
				line += " [" + nefarius::utilities::ConvertWideToANSI(event.Subject) + "]";
			}

			line += ": ";
			line += event.Message;

			if (event.Win32Code.has_value() && event.Win32Code.value() != ERROR_SUCCESS)
			{
				line += std::format(" (0x{:08X})", event.Win32Code.value());
			}

			switch (event.Level)
			{
			case DiagnosticLevel::Verbose:
				logger->verbose(1, "%v", line);
				break;
			case DiagnosticLevel::Info:
				logger->info("%v", line);
				break;
			case DiagnosticLevel::Warning:
				logger->warn("%v", line);
				break;
			case DiagnosticLevel::Error:
				logger->error("%v", line);
				break;
			}
		});
	}
}
