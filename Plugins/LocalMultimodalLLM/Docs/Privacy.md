# Privacy

Local Multimodal LLM is designed for private, offline execution. The plug-in
does not send prompts, microphone audio, generated dialogue, character memory,
model telemetry, or personal information to Ian Sundahl or Volley Studios.
The included Starter stack does not require an online account or network
service at runtime.

Microphone capture and inference occur on the user's machine. Conversation
state remains in the running Unreal application unless the integrating game
explicitly saves, logs, transmits, or otherwise processes it. The developer of
that game is responsible for disclosing and securing any additional analytics,
cloud providers, saved voice data, multiplayer transport, or external APIs
they add.

Fab, Epic Games, GitHub, operating-system speech devices, crash reporters, and
download hosts may process information under their own terms. Those services
are separate from the plug-in runtime.

Privacy or security questions about the original plug-in can be reported at:
https://github.com/isundahl/LLM_Plugin/issues

